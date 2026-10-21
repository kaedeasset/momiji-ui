#!/usr/bin/env node
// momiji-ui/conformance.mjs — MomijiUI の照合のブラウザ側と突き合わせ。**直叩きせず Scripts/momiji-ui-conformance.ps1 から**。
// 仕様: Docs/Guides/Working/momiji-ui-framework.md「照合」。フィクスチャ: Plugins/MomijiUI/Fixtures/*.json
//
//   node Tools/momiji-ui/conformance.mjs --chrome <chrome の exe> --port <n> [--canvas <momiji-ui-canvas.js の別の置き場>] [--engine chrome|chromium|firefox|webkit]
//     --canvas は「直す前の描画器で期待値が FAIL になること」を確かめるため（例: git show HEAD~1:Scripts/lib/momiji-ui-canvas.js > old.js）
//     --engine は chrome（既定・OS の Chrome を DevTools 直結 cdp.mjs で）か、Playwright の chromium / firefox / webkit（pw.mjs。
//       Tools/momiji-ui で npm install と npx playwright install が要る）。chrome 以外の結果は <id>.<engine>.json / .png に書く
//
// 1. 小さな静的サーバで momiji-ui-canvas.js・同梱書体・フィクスチャを出す
// 2. ヘッドレス Chrome で各フィクスチャを配置し、Saved/MomijiUIConformance/<id>.web.json に書く:
//      hits … action が "t:" の当たり（鍵・矩形・スクロール・焦点の群・上下左右の移動先）
//      prims … 描画命令の全部（矩形・枠・文字の行・画像・線・楕円）。間引きを止めて（cull=false）全部描いて記録する
//      scenarios … フィクスチャの "scenarios" を **attach 越し**（本物の <canvas> と <input>・DevTools の本物のマウスとキー）で通した結果
//                  （各手の焦点の鍵・最前面の群・スクロール量・押した鍵・測った高さ・編集中の欄・欄の中身・押された数・編集器の見える矩形）
// 3. UE 側（自動テスト MomijiUI.MomijiUIConformance が書いた <id>.ue.json）と突き合わせ、さらに **各手の expect（仕様から決めた期待値）と両実装を別々に比べる**:
//      当たりは鍵（同じ鍵が複数なら出た順の番号を添える）で対応づけ、矩形は 1px 以内・スクロールと群と移動先は同じ
//      描画命令は種類（と文字）ごとに、位置 1px 以内で 1 対 1 に対応づく（余りがあれば FAIL）
//      シナリオは手ごとに、焦点の鍵と群と押した鍵と編集中の欄と欄の中身と押された数と文字の選択（器・範囲・写す文）が同じ・スクロール量と高さと編集器の矩形は 1px 以内
//      expect は UE・ブラウザそれぞれと比べる（両方が同じ誤りを持っていても一致だけでは PASS にしない・2026-10-04）
// 出力の末尾: MOMIJIUI PASS fixtures=N hits=M prims=K steps=S / MOMIJIUI FAIL …（終了コード 1）
//
//   node Tools/momiji-ui/conformance.mjs --chrome <chrome の exe> --port <n> --shots <撮影の表 JSON> --out <置き場>   （2026-10-21）
//     照合をせず、撮影の表の "web" の画面を 1920×1080（論理 1280×720 を 1.5 倍）で撮って <置き場>/<name>.png に書く（説明書の絵。
//     設計 Docs/Guides/Working/free-plugins-fab-docs-design.md §5.7）。表の 1 件は { name, screen（根からの道）, theme?, bind?, diagnose? }。
//     diagnose（根からの道）があれば、その JSON の行を lists.source（n・text）に、validate の結果を lists.diags（severity・code・path・message）に入れて描く。
//     末尾の行は SHOTS PASS n=<枚数> / SHOTS FAIL

import fs from 'node:fs';
import path from 'node:path';
import { waitFor, sleep } from './cdp.mjs';
import { classicFrom } from './web/build.mjs';
import { ROOT, SRC, CLASSIC, SAVED, fontFaceCss, fontRoutes } from './lib/paths.mjs';
import { serveStatic } from './lib/static-server.mjs';
import { parseArgs } from './lib/args.mjs';
import { openDriver } from './lib/driver.mjs';

const FIX = path.join(ROOT, 'Plugins', 'MomijiUI', 'Fixtures');
const OUT = path.join(SAVED, 'MomijiUIConformance');
const TOL = 1;
const TEXT_TOL = 2;   // 文字の位置だけ 2px: 送り幅の計測が描画系で 1〜2px 違い、中央・右寄せの文字がその分ずれる（仕様 §9）

const args = () => parseArgs(process.argv.slice(2), { defaults: { engine: 'chrome' }, required: (o) => (o.engine === 'chrome' ? ['chrome', 'port'] : ['port']) });

// 照合の頁: 書体を読み、フィクスチャごとに配置・記録・シナリオを通す。
// シナリオは見本や ADV と同じ形（stage の中に canvas・attach が <input> を stage へ足す）で、入力は Node 側が DevTools で本物を送る
const PAGE = `<!doctype html><meta charset="utf-8"><style>
${fontFaceCss()}
body { margin: 0; background: #000; }
#stage { position: relative; width: 1280px; height: 720px; }
#stage input { background: transparent; color: #e6edf6; border: 0; outline: none; padding: 0 12px; font-family: "KaedeGothic", sans-serif; }</style>
<div id="stage"><canvas id="c" width="1280" height="720"></canvas></div>
<script src="momiji-ui-canvas.js"></script>
<script>
const merge = (v, b) => {   // bind を値へ重ねる（strings / vars / lists は鍵ごとに置き換え）
  for (const k of ['strings', 'vars', 'lists']) if (b && b[k]) Object.assign(v[k], b[k]);
  return v;
};
const values = (f) => merge({ strings: {}, vars: {}, lists: {} }, f.bind);
const itemOf = (o) => { const s = {}; for (const k of Object.keys(o || {})) s[k] = typeof o[k] === 'string' ? o[k] : JSON.stringify(o[k]); return s; };
window.run = async (fixtures) => {
  await Promise.all([document.fonts.load('16px "KaedeGothic"'), document.fonts.load('700 16px "KaedeGothic"')]);
  const out = {};
  for (const f of fixtures) {
    // ---- 配置: 当たりと描画命令（間引きを止めて全部描く＝UE の描画命令と同じ範囲）----
    const R = new MomijiUICanvas.Renderer(document.getElementById('c'));
    R.setTheme('midnight'); R.domTextInputs = true; R.cull = false; R.recording = true; R.screenId = f.id;
    MomijiUICanvas.present(R, f.root, values(f), null);
    const hits = R.hits.map((h, i) => ({ h, i })).filter(({ h }) => h.action.startsWith('t:')).map(({ h, i }) => {
      const to = (dx, dy) => { const j = R.focusNavFrom(i, dx, dy); return j >= 0 ? R.hits[j].key : ''; };
      return { key: h.key, action: h.action, x: h.x, y: h.y, w: h.w, h: h.h, scroll: h.scrollId || '', group: h.group || 0,
               up: to(0, -1), down: to(0, 1), left: to(-1, 0), right: to(1, 0) };
    });
    out[f.id] = { hits, prims: R.prims.slice() };
  }
  return out;
};
// ---- 操作シナリオ（attach 越し）。Node 側が scnBegin → 手ごとに scnStep か DevTools の入力 → scnRecord → scnEnd の順に呼ぶ ----
let scn = null;
document.addEventListener('compositionstart', () => { if (scn) scn.imeSeen = true; }, true);
document.addEventListener('keydown', (e) => { if (scn) scn.lastKeyComposing = !!e.isComposing; }, true);
window.scnBegin = (f) => {
  const canvas = document.getElementById('c');
  const S = new MomijiUICanvas.Renderer(canvas);
  S.setTheme('midnight'); S.domTextInputs = true; S.screenId = f.id;
  const v = values(f);
  const present = () => MomijiUICanvas.present(S, f.root, v, null);
  scn = { S, v, present, events: 0, imeSeen: false, lastKeyComposing: null };
  scn.host = MomijiUICanvas.attach(S, canvas, { stage: document.getElementById('stage'), render: present, onEvent: () => { scn.events++; }, animate: false });
  present();
  return true;
};
window.scnStep = (st) => {   // DevTools で送らない手
  const { S, v, host, present } = scn; const r = {};
  if (st.bind) { merge(v, st.bind); present(); }
  else if (st.scroll) { S.scroll.set(st.scroll.id, Math.max(0, st.scroll.y)); present(); }
  else if (st.hit) { const h = S.hitAt(st.hit[0], st.hit[1]); r.hit = h ? h.key : ''; }
  else if (st.measure) {
    let n = MomijiUICanvas.cloneTree(st.measure.node);
    if (st.measure.item) n = MomijiUICanvas.instantiateTemplate(n, itemOf(st.measure.item));
    r.h = S.measureNode(n);
  }
  else if (st.probe) {   // その点で入力を受けるのは誰か（DOM の判定＝ elementFromPoint）
    const el = document.elementFromPoint(st.probe[0], st.probe[1]);
    r.under = el && el.tagName === 'INPUT' ? 'input:' + (el.dataset.bindVar || '') : 'canvas';
  }
  else if (st.screen !== undefined) { S.screenId = st.screen; present(); }
  else if (st.reset !== undefined) { if (S.resetScreen) S.resetScreen(st.reset); present(); }   // 直す前の描画器（--canvas）には無い＝何もせず、期待と合わず FAIL する
  else if (st.input) { host.setInputValue(st.input.id, st.input.value); }
  else if (st.imeEnterSynthetic) {   // 変換中の Enter（合成イベント。DevTools の IME が使えないときの代わり）
    const a = document.activeElement;
    if (a && a.tagName === 'INPUT') a.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', isComposing: true, bubbles: true, cancelable: true }));
    r.ime = 'synthetic';
  }
  return r;
};
window.scnRecord = (r) => {
  const { S, host } = scn;
  const scroll = {}; for (const a of S.scrollAreas) scroll[a.id] = S.scroll.get(a.id) || 0;
  const inputs = {}; for (const t of S.textInputs) if (t.bindVar) inputs[t.bindVar] = host.inputValue(t.bindVar);
  // 直す前の描画器（--canvas）には activeInput が無い: DOM から読む（bindVar の印も無いので '?' になり、期待と合わず FAIL する＝それでよい）
  const active = host.activeInput ? host.activeInput() : (() => { const a = document.activeElement; return a && a.tagName === 'INPUT' ? (a.dataset.bindVar || '?') : ''; })();
  const at = active ? S.textInputs.find((t) => t.bindVar === active) : null;
  // 文字の選択（2026-10-07）: { group, from: [行, 字], to: [行, 字], text }。無ければ null（直す前の描画器には selection が無い＝undefined → null）
  const sel = S.selection ? { group: S.selection.group, from: S.selection.from, to: S.selection.to, text: S.selection.text } : null;
  return Object.assign(r, { focus: S.focusKey, top: S.topFocusGroup(), scroll, active, inputs, events: scn.events, editor: at && at.clip ? at.clip : null, sel });
};
window.scnIme = () => ({ seen: scn.imeSeen, composing: scn.lastKeyComposing });
window.scnEnd = () => { scn.host.detach(); scn = null; return true; };
// 診断（UE の validateScreen と code/path を突き合わせる）
window.diags = (fixtures) => { const o = {}; for (const f of fixtures) o[f.id] = MomijiUICanvas.validate(f).map((d) => ({ severity: d.severity, code: d.code, path: d.path })); return o; };
// 絵を 1 枚: 画像の読み込みと動き（出現・脈動）を描き直しで進めてから止める
window.shot = async (f, waitMs) => {
  const R = new MomijiUICanvas.Renderer(document.getElementById('c'));
  R.setTheme('midnight'); R.screenId = f.id + '-shot';
  const tree = MomijiUICanvas.prepare(f.root, values(f));
  const until = performance.now() + waitMs;
  await new Promise((done) => { const tick = () => { R.draw(tree, 'rgba(14,17,22,1)'); if (performance.now() < until) requestAnimationFrame(tick); else done(); }; tick(); });
  return true;
};
</script>`;

// 説明書の絵の頁（--shots）: 1920×1080 の canvas に論理 1280×720 を 1.5 倍で描く。動き（出現・脈動）は waitMs だけ描き直して進める
const SHOTS_PAGE = `<!doctype html><meta charset="utf-8"><style>
${fontFaceCss()}
body { margin: 0; background: #000; }</style>
<canvas id="c" width="1920" height="1080"></canvas>
<script src="momiji-ui-canvas.js"></script>
<script>
window.docShot = async (s, waitMs) => {
  await Promise.all([document.fonts.load('16px "KaedeGothic"'), document.fonts.load('700 16px "KaedeGothic"')]);
  const v = { strings: {}, vars: {}, lists: {} };
  for (const k of ['strings', 'vars', 'lists']) if (s.bind && s.bind[k]) Object.assign(v[k], s.bind[k]);
  if (s.diagnose) {
    v.lists.source = s.diagnose.text.replace(/\\r/g, '').replace(/\\n$/, '').split('\\n').map((t, i) => ({ n: String(i + 1).padStart(2, ' '), text: t }));
    v.lists.diags = MomijiUICanvas.validate(s.diagnose.json).map((d) => ({ severity: d.severity, code: d.code, path: d.path, message: d.message }));
  }
  const R = new MomijiUICanvas.Renderer(document.getElementById('c'));
  R.setTheme(s.theme || 'midnight'); R.setScale(1.5); R.screenId = 'shot-' + s.name;
  const tree = MomijiUICanvas.prepare(s.screen.root, v);
  const until = performance.now() + waitMs;
  await new Promise((done) => { const tick = () => { R.draw(tree, 'rgba(14,17,22,1)'); if (performance.now() < until) requestAnimationFrame(tick); else done(); }; tick(); });
  return { diags: v.lists.diags ? v.lists.diags.length : 0 };
};
</script>`;

function serve(port, canvasFile) {
	return serveStatic(port, {
		routes: Object.assign({
			'/index.html': { type: 'text/html; charset=utf-8', body: () => PAGE },
			'/shots.html': { type: 'text/html; charset=utf-8', body: () => SHOTS_PAGE },
			'/momiji-ui-canvas.js': canvasFile || CLASSIC,
		}, fontRoutes()),
		// フィクスチャの画像（sprite は UE と同じくプロジェクト相対の "Game/...png" か "Plugins/...png"）
		resolve: (url) => (/^\/(Game|Plugins)\/[\w\-\/]+\.png$/.test(url) ? path.join(ROOT, url) : null),
		notFound: '',
	});
}
// 変換中の Enter。ブラウザの IME の口（imeSetComposition）で本当に composition が起きて keydown が isComposing になるかを先に 1 度確かめ（probeIme）、
// 起きない（口が無い）ならページで合成イベントを送る（どちらを使ったかは web.json の各手の ime に残す）
async function probeIme(d, fixture) {
	if (!d.imeSetComposition) return false;
	await d.evalJs(`scnBegin(${JSON.stringify(fixture)})`);
	let ok = false;
	try {
		const t = await d.evalJs(`(() => { const t = scn.S.textInputs.find((x) => x.bindVar && scn.S.inputInteractive(x)); return t ? { x: t.x + t.w / 2, y: t.y + t.h / 2 } : null; })()`);
		if (t) {
			await d.click(t.x, t.y);
			await d.imeSetComposition('か');
			await sleep(30);
			await d.key('Enter');
			await sleep(30);
			const r = await d.evalJs('scnIme()');
			ok = !!(r.seen && r.composing);
			await d.imeClose();   // 変換を閉じる
		}
	} catch { ok = false; }
	await d.evalJs('scnEnd()');
	return ok;
}
async function imeEnter(d, viaIme) {
	if (!viaIme) return d.evalJs('scnStep({imeEnterSynthetic:true})');
	await d.imeSetComposition('か');
	await sleep(30);
	await d.key('Enter');   // 変換中の Enter＝確定であって送信ではない
	await sleep(30);
	await d.imeClose();   // 変換を閉じる（何も確定しない＝欄の中身は変わらない。UE はこの手で何もしないので同じになる）
	return { ime: 'cdp' };
}
async function runScenario(d, f, sc, viaIme) {
	await d.evalJs(`scnBegin(${JSON.stringify(f)})`);
	const steps = [await d.evalJs('scnRecord({})')];
	for (const st of (sc.steps || [])) {
		let r = {};
		if (st.click) await d.click(st.click[0], st.click[1]);
		else if (st.select) await d.drag(st.select[0][0], st.select[0][1], st.select[1][0], st.select[1][1]);
		else if (st.move) await d.key(st.move[1] < 0 ? 'ArrowUp' : st.move[1] > 0 ? 'ArrowDown' : st.move[0] < 0 ? 'ArrowLeft' : 'ArrowRight');
		else if (st.enter) await d.key('Enter');
		else if (st.tab) await d.key('Tab');
		else if (st.type !== undefined) await d.type(String(st.type));
		else if (st.imeEnter) r = await imeEnter(d, viaIme);
		else r = await d.evalJs(`scnStep(${JSON.stringify(st)})`);
		await sleep(30);
		steps.push(await d.evalJs(`scnRecord(${JSON.stringify(r)})`));
	}
	await d.evalJs('scnEnd()');
	return { name: sc.name || '', steps };
}

const near = (a, b, tol) => Math.abs((a || 0) - (b || 0)) <= (tol || TOL);
// 値の比較は鍵の順に依らない（UE の nlohmann は鍵をアルファベット順に書く）。文字の選択 sel の突き合わせ用
const canon = (v) => JSON.stringify(v ?? null, (k, x) => (x && typeof x === 'object' && !Array.isArray(x) ? Object.fromEntries(Object.keys(x).sort().map((key) => [key, x[key]])) : x));

// 同じ鍵が複数（id 無しで同じ action）なら出た順の番号を添える＝両方とも同じ順で並ぶので対応づく
function numbered(hits) {
	const seen = new Map();
	return hits.map((h) => { const n = (seen.get(h.key) || 0) + 1; seen.set(h.key, n); return Object.assign({}, h, { k: n > 1 ? `${h.key}#${n}` : h.key }); });
}
function compareHits(ue, web) {
	const msgs = [];
	const U = numbered(ue), W = numbered(web);
	const wm = new Map(W.map((h) => [h.k, h]));
	for (const u of U) {
		const w = wm.get(u.k);
		if (!w) { msgs.push(`当たり ${u.k}: ブラウザに無い`); continue; }
		const d = ['x', 'y', 'w', 'h'].filter((p) => !near(u[p], w[p]));
		if (d.length) msgs.push(`当たり ${u.k}: ${d.map((p) => `${p} UE=${u[p]} web=${w[p]}`).join(' ')}`);
		if ((u.scroll || '') !== (w.scroll || '')) msgs.push(`当たり ${u.k}: scroll UE=${u.scroll} web=${w.scroll}`);
		if ((u.group || 0) !== (w.group || 0)) msgs.push(`当たり ${u.k}: group UE=${u.group} web=${w.group}`);
		for (const dir of ['up', 'down', 'left', 'right']) if ((u[dir] || '') !== (w[dir] || '')) msgs.push(`当たり ${u.k}: ${dir} UE=${u[dir] || '-'} web=${w[dir] || '-'}`);
	}
	const um = new Set(U.map((h) => h.k));
	for (const w of W) if (!um.has(w.k)) msgs.push(`当たり ${w.k}: UE に無い`);
	return msgs;
}
// 描画命令: 種類（と文字）が同じで位置が 1px 以内のものを 1 対 1 に対応づける。余りは FAIL
function comparePrims(ue, web) {
	const msgs = [];
	const rest = web.slice();
	const same = (u, w) => u.kind === w.kind && (u.kind === 'Text'
		? (u.text === w.text && near(u.x, w.x, TEXT_TOL) && near(u.y, w.y, TEXT_TOL))
		: (near(u.x, w.x) && near(u.y, w.y) && near(u.w, w.w) && near(u.h, w.h)));
	const describe = (p) => `${p.kind}${p.text !== undefined ? ` "${p.text}"` : ''} (${p.x},${p.y}${p.w !== undefined ? `,${p.w}x${p.h}` : ''})`;
	let unmatched = 0;
	for (const u of ue) {
		const i = rest.findIndex((w) => same(u, w));
		if (i < 0) { if (unmatched++ < 12) msgs.push(`描画 UE だけ: ${describe(u)}`); continue; }
		rest.splice(i, 1);
	}
	let extra = 0;
	for (const w of rest) if (extra++ < 12) msgs.push(`描画 ブラウザだけ: ${describe(w)}`);
	if (unmatched > 12) msgs.push(`描画 UE だけ: ほか ${unmatched - 12} 件`);
	if (extra > 12) msgs.push(`描画 ブラウザだけ: ほか ${extra - 12} 件`);
	return msgs;
}
// 診断: severity・code・path の列が同じ
function compareDiags(ue, web) {
	const key = (d) => `${d.severity} ${d.code} ${d.path}`;
	const U = (ue || []).map(key), W = (web || []).map(key);
	const msgs = [];
	for (const k of U) if (!W.includes(k)) msgs.push(`診断 UE だけ: ${k}`);
	for (const k of W) if (!U.includes(k)) msgs.push(`診断 ブラウザだけ: ${k}`);
	return msgs;
}
const sameRect = (a, b) => (a == null && b == null) || (a != null && b != null && near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h));
const showRect = (r) => (r == null ? 'null' : `(${r.x},${r.y},${r.w}x${r.h})`);
function compareScenarios(ue, web) {
	const msgs = [];
	const wm = new Map(web.map((s) => [s.name, s]));
	for (const u of ue) {
		const w = wm.get(u.name);
		if (!w) { msgs.push(`シナリオ ${u.name}: ブラウザに無い`); continue; }
		if (u.steps.length !== w.steps.length) { msgs.push(`シナリオ ${u.name}: 手の数 UE=${u.steps.length} web=${w.steps.length}`); continue; }
		u.steps.forEach((us, i) => {
			const ws = w.steps[i];
			if ((us.focus || '') !== (ws.focus || '')) msgs.push(`シナリオ ${u.name} 手${i}: 焦点 UE=${us.focus || '-'} web=${ws.focus || '-'}`);
			if ((us.top || 0) !== (ws.top || 0)) msgs.push(`シナリオ ${u.name} 手${i}: 最前面の群 UE=${us.top} web=${ws.top}`);
			if (us.hit !== undefined && (us.hit || '') !== (ws.hit || '')) msgs.push(`シナリオ ${u.name} 手${i}: 押した鍵 UE=${us.hit || '-'} web=${ws.hit || '-'}`);
			if (us.h !== undefined && !near(us.h, ws.h)) msgs.push(`シナリオ ${u.name} 手${i}: 高さ UE=${us.h} web=${ws.h}`);
			if (us.under !== undefined && (us.under || '') !== (ws.under || '')) msgs.push(`シナリオ ${u.name} 手${i}: 点の下 UE=${us.under || '-'} web=${ws.under || '-'}`);
			if ((us.active || '') !== (ws.active || '')) msgs.push(`シナリオ ${u.name} 手${i}: 編集中の欄 UE=${us.active || '-'} web=${ws.active || '-'}`);
			if ((us.events || 0) !== (ws.events || 0)) msgs.push(`シナリオ ${u.name} 手${i}: 押された数 UE=${us.events || 0} web=${ws.events || 0}`);
			if (!sameRect(us.editor, ws.editor)) msgs.push(`シナリオ ${u.name} 手${i}: 編集器の矩形 UE=${showRect(us.editor)} web=${showRect(ws.editor)}`);
			if (canon(us.sel) !== canon(ws.sel)) msgs.push(`シナリオ ${u.name} 手${i}: 選択 UE=${JSON.stringify(us.sel ?? null)} web=${JSON.stringify(ws.sel ?? null)}`);
			for (const id of new Set([...Object.keys(us.inputs || {}), ...Object.keys(ws.inputs || {})]))
				if ((us.inputs?.[id] ?? null) !== (ws.inputs?.[id] ?? null)) msgs.push(`シナリオ ${u.name} 手${i}: 欄 ${id} の中身 UE=${JSON.stringify(us.inputs?.[id] ?? null)} web=${JSON.stringify(ws.inputs?.[id] ?? null)}`);
			for (const id of new Set([...Object.keys(us.scroll || {}), ...Object.keys(ws.scroll || {})]))
				if (!near(us.scroll?.[id], ws.scroll?.[id])) msgs.push(`シナリオ ${u.name} 手${i}: スクロール ${id} UE=${us.scroll?.[id]} web=${ws.scroll?.[id]}`);
		});
	}
	return msgs;
}
// 期待値（フィクスチャの各手の expect）と 1 つの実装の記録を比べる。手 0 は描いた直後（expect は steps[0] の手に書く＝記録の 1 番目）。
// 鍵ごとに: focus/top/hit/under/active は同じ文字列、events は同じ数、scroll は 1px 以内、inputs は同じ文字列（expect にある鍵だけ）、editor は null か矩形 1px 以内
function checkExpect(impl, fixture, rec) {
	const msgs = [];
	for (const sc of (fixture.scenarios || [])) {
		const r = (rec || []).find((s) => s.name === (sc.name || ''));
		if (!r) { msgs.push(`期待(${impl}) シナリオ ${sc.name}: 記録が無い`); continue; }
		(sc.steps || []).forEach((st, i) => {
			const e = st.expect; if (!e) return;
			const got = r.steps[i + 1]; if (!got) { msgs.push(`期待(${impl}) シナリオ ${sc.name} 手${i + 1}: 記録が無い`); return; }
			const bad = (k, want, have) => msgs.push(`期待(${impl}) シナリオ ${sc.name} 手${i + 1}: ${k} 期待=${JSON.stringify(want)} 実=${JSON.stringify(have)}`);
			for (const k of ['focus', 'top', 'hit', 'under', 'active']) if (k in e && (e[k] ?? '') !== (got[k] ?? '')) bad(k, e[k], got[k]);
			if ('events' in e && (e.events || 0) !== (got.events || 0)) bad('events', e.events, got.events);
			if ('h' in e && !near(e.h, got.h)) bad('h', e.h, got.h);
			if ('scroll' in e) for (const id of Object.keys(e.scroll)) if (!near(e.scroll[id], got.scroll?.[id])) bad(`scroll ${id}`, e.scroll[id], got.scroll?.[id]);
			if ('inputs' in e) for (const id of Object.keys(e.inputs)) if ((e.inputs[id] ?? null) !== (got.inputs?.[id] ?? null)) bad(`inputs ${id}`, e.inputs[id], got.inputs?.[id] ?? null);
			if ('editor' in e && !sameRect(e.editor, got.editor)) bad('editor', e.editor, got.editor);
			if ('sel' in e && canon(e.sel) !== canon(got.sel)) bad('sel', e.sel, got.sel ?? null);
		});
	}
	return msgs;
}

// --shots: 撮影の表の "web" を撮る（照合はしない）。Chrome だけ（説明書の絵は 1 つのブラウザで撮れば足りる）
async function runShots(o) {
	if (o.engine !== 'chrome') { console.log('SHOTS FAIL: --shots は --engine chrome だけ'); process.exit(1); }
	if (!o.out) { console.log('SHOTS FAIL: --out が要る'); process.exit(2); }
	const table = JSON.parse(fs.readFileSync(path.resolve(ROOT, o.shots), 'utf8'));
	const read = (p) => fs.readFileSync(path.join(ROOT, p), 'utf8');
	const list = (table.web || []).map((s) => Object.assign({}, s, {
		screen: JSON.parse(read(s.screen)),
		diagnose: s.diagnose ? { text: read(s.diagnose), json: JSON.parse(read(s.diagnose)) } : undefined,
	}));
	const out = path.resolve(ROOT, o.out);
	fs.mkdirSync(out, { recursive: true });
	const port = Number(o.port);
	const srv = await serve(port);
	const d = await openDriver('chrome', { chromeExe: o.chrome, port: port + 1 });
	let n = 0;
	try {
		await d.viewport(1920, 1080);
		await d.navigate(`http://127.0.0.1:${port}/shots.html`);
		await waitFor(() => d.evalJs('typeof window.docShot === "function"'), 30000, 'page');
		for (const s of list) {
			const r = await d.evalJs(`docShot(${JSON.stringify(s)}, 1500)`);
			fs.writeFileSync(path.join(out, `${s.name}.png`), await d.screenshot());
			console.log(`SHOT: ${s.name}${s.diagnose ? `（診断 ${r.diags}）` : ''}`);
			n++;
		}
	} finally {
		await d.close();
		srv.close();
	}
	console.log(`SHOTS PASS n=${n} -> ${path.relative(ROOT, out)}`);
}

async function main() {
	const o = args();
	if (o.shots) return runShots(o);
	const port = Number(o.port);
	fs.mkdirSync(OUT, { recursive: true });
	const fixtures = fs.readdirSync(FIX).filter((f) => f.endsWith('.json')).sort()
		.map((f) => JSON.parse(fs.readFileSync(path.join(FIX, f), 'utf8')));
	if (o.canvas) console.log(`描画器: ${o.canvas}（既定の Scripts/lib/momiji-ui-canvas.js ではない）`);
	else {
		// 生成物（Scripts/lib/momiji-ui-canvas.js）が源（Tools/momiji-ui/web/src/momiji-ui.mjs）から作ったものと同じでなければ、照合の相手が古い
		const fresh = classicFrom(fs.readFileSync(SRC, 'utf8'));
		if (fresh !== fs.readFileSync(CLASSIC, 'utf8')) {
			console.log('FAIL: Scripts/lib/momiji-ui-canvas.js が源（Tools/momiji-ui/web/src/momiji-ui.mjs）より古い。node Tools/momiji-ui/web/build.mjs で作り直してから');
			console.log('MOMIJIUI FAIL');
			process.exit(1);
		}
	}
	const srv = await serve(port, o.canvas);
	// ブラウザの口（lib/driver.mjs）: chrome は OS の Chrome を DevTools 直結で、ほかは Playwright（pw.mjs）
	const tag = o.engine === 'chrome' ? 'web' : o.engine;   // 出力の名前 <id>.<tag>.json / .png
	const d = await openDriver(o.engine, { chromeExe: o.chrome, port: port + 1 });
	console.log(`ブラウザ: ${o.engine}（${d.version}）`);
	let web, imeViaCdp = false;
	try {
		await d.navigate(`http://127.0.0.1:${port}/`);
		await waitFor(() => d.evalJs('typeof window.run === "function"'), 30000, 'page');
		const valid = fixtures.filter((f) => !f.invalid);
		web = await d.evalJs(`run(${JSON.stringify(valid)})`);
		// 操作シナリオ（attach 越し・本物の入力）。IME の手はブラウザの IME の口が効くかを先に確かめる
		const needIme = valid.find((f) => (f.scenarios || []).some((s) => (s.steps || []).some((st) => st.imeEnter)));
		if (needIme) { imeViaCdp = await probeIme(d, needIme); console.log(`IME: 変換中の Enter は ${imeViaCdp ? 'ブラウザの IME（本物の composition）' : '合成イベント（このブラウザには IME の口が無い）'} で送る`); }
		for (const f of valid) {
			web[f.id].scenarios = [];
			for (const sc of (f.scenarios || [])) web[f.id].scenarios.push(await runScenario(d, f, sc, imeViaCdp));
		}
		const diags = await d.evalJs(`diags(${JSON.stringify(fixtures)})`);
		for (const f of fixtures) { web[f.id] = web[f.id] || {}; web[f.id].diags = diags[f.id]; }
		// 絵（描き方の見本。合否には入れない・人が見る）: <id>.<tag>.png
		for (const f of valid) {
			await d.evalJs(`shot(${JSON.stringify(f)}, 1500)`);
			fs.writeFileSync(path.join(OUT, `${f.id}.${tag}.png`), await d.screenshot());
		}
	} finally {
		await d.close();
		srv.close();
	}
	let fails = 0, hits = 0, prims = 0, steps = 0, expects = 0;
	for (const f of fixtures) {
		const w = web[f.id];
		w.engine = o.engine; w.browser = d.version; w.imeViaCdp = imeViaCdp;
		fs.writeFileSync(path.join(OUT, `${f.id}.${tag}.json`), JSON.stringify(w, null, 1) + '\n');
		const ueFile = path.join(OUT, `${f.id}.ue.json`);
		// ブラウザの期待値は UE 側が無くても見る（直しているときに先に分かる）
		const webExpect = f.invalid ? [] : checkExpect(o.engine === 'chrome' ? 'web' : o.engine, f, w.scenarios);
		if (!fs.existsSync(ueFile)) { console.log(`FAIL: ${f.id} — UE 側が無い（自動テスト MomijiUI.MomijiUIConformance を先に）${webExpect.length ? `・ブラウザの期待 ${webExpect.length} 件`: ''}`); for (const m of webExpect) console.log(`  ${m}`); fails++; continue; }
		const u = JSON.parse(fs.readFileSync(ueFile, 'utf8'));
		if (!u.diags) { console.log(`FAIL: ${f.id} — UE 側が古い形（diags が無い）。UE を作り直してから`); fails++; continue; }
		if (f.invalid) {   // わざと壊したフィクスチャ: 診断だけ突き合わせ、error が出ていること
			const msgs = compareDiags(u.diags, w.diags);
			if (!(w.diags || []).some((d) => d.severity === 'error')) msgs.push('診断: error が 1 件も出ていない');
			if (msgs.length) { fails++; console.log(`FAIL: ${f.id}（${msgs.length}）`); for (const m of msgs) console.log(`  ${m}`); }
			else console.log(`PASS: ${f.id}（診断 ${w.diags.length}）`);
			continue;
		}
		const msgs = [...compareDiags(u.diags, w.diags), ...compareHits(u.hits, w.hits), ...comparePrims(u.prims, w.prims), ...compareScenarios(u.scenarios, w.scenarios),
			...checkExpect('UE', f, u.scenarios), ...webExpect];
		if ((w.diags || []).some((d) => d.severity === 'error')) msgs.push('診断: フィクスチャに error がある');
		hits += w.hits.length; prims += w.prims.length; steps += w.scenarios.reduce((n, s) => n + s.steps.length, 0);
		const nExpect = (f.scenarios || []).reduce((n, s) => n + (s.steps || []).filter((st) => st.expect).length, 0);
		expects += nExpect;
		if (msgs.length) { fails++; console.log(`FAIL: ${f.id}（${msgs.length}）`); for (const m of msgs) console.log(`  ${m}`); }
		else console.log(`PASS: ${f.id}（当たり ${w.hits.length}・描画 ${w.prims.length}・手 ${w.scenarios.reduce((n, s) => n + s.steps.length, 0)}・期待 ${nExpect}）`);
	}
	console.log(fails ? `MOMIJIUI FAIL fixtures=${fixtures.length} fail=${fails} engine=${o.engine}` : `MOMIJIUI PASS fixtures=${fixtures.length} hits=${hits} prims=${prims} steps=${steps} expects=${expects} engine=${o.engine}`);
	process.exit(fails ? 1 : 0);
}

main().catch((e) => { console.error(e); console.log('MOMIJIUI FAIL'); process.exit(1); });
