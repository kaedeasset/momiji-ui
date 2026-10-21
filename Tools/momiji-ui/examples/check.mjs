#!/usr/bin/env node
// MomijiUI の見本（ブラウザ）が説明書どおりに動くかを、本物のクリックと文字入力で確かめる。
// **直叩きせず Scripts/momiji-ui-examples.ps1 から**（UE の見本の自動テストと一緒に回す）。
//
//   node Tools/momiji-ui/examples/check.mjs --chrome <chrome の exe> --port <n>
//
// 見るもの（説明書 §3 の手順の結果）: +1 を 2 回押すと 2・リセットが押せるようになる・
// 文字欄に打って Enter で一覧に足され欄が空に戻る・リセットで 0。絵は Saved/MomijiUIExamples/counter.web.png
// 出力の末尾: EXAMPLES PASS checks=N / EXAMPLES FAIL …（終了コード 1）
import fs from 'node:fs';
import path from 'node:path';
import { waitFor, sleep } from '../cdp.mjs';
import { serveExamples } from './serve.mjs';
import { SAVED } from '../lib/paths.mjs';
import { parseArgs } from '../lib/args.mjs';
import { openDriver } from '../lib/driver.mjs';

const OUT = path.join(SAVED, 'MomijiUIExamples');
const o = parseArgs(process.argv.slice(2), { required: () => ['chrome', 'port'] });

let fails = 0, checks = 0;
const check = (name, ok, detail = '') => { checks++; if (ok) console.log(`PASS: ${name}`); else { fails++; console.log(`FAIL: ${name}${detail ? ' — ' + detail : ''}`); } };

const port = Number(o.port);
const srv = await serveExamples(port);
const c = await openDriver('chrome', { chromeExe: o.chrome, port: port + 1 });   // 照合と同じ口（本物のクリック・打鍵・キー）
try {
	await c.navigate(`http://127.0.0.1:${port}/counter/`);
	await waitFor(() => c.evalJs('!!window.example'), 30000, 'example');
	await sleep(300);
	// 論理座標の当たりの中心を、ページの座標（窓 1280×720 なので等倍）で本物のクリックにする
	const hitCenter = (action) => c.evalJs(`(() => { const h = window.example.renderer.hits.find((x) => x.action === ${JSON.stringify(action)}); return h ? { x: h.x + h.w / 2, y: h.y + h.h / 2 } : null; })()`);
	const click = async (action) => {
		const p = await hitCenter(action);
		if (!p) return false;
		await c.click(p.x, p.y);
		await sleep(100);
		return true;
	};
	const state = () => c.evalJs('JSON.parse(JSON.stringify(window.example.state))');

	check('最初はリセットが押せない（enabledIf: count > 0）', !(await hitCenter('count:reset')));
	// 脈動は部品の動きの記録（motion の pulseOn）で見る。renderer.pulsing はホバー・押下の光りでも立つ
	const pulsing = () => c.evalJs(`!!(window.example.renderer.motion.get('counter/count:add') || {}).pulseOn`);
	check('押す前は +1 が脈動している（anim.pulse の whileIf: count == 0）', await pulsing());
	await click('count:add'); await click('count:add');
	await sleep(400);   // 離した瞬間の光り（150ms）とホバーの明るさ（120ms）が落ち着くまで
	check('押したら脈動は止まる', !(await pulsing()));
	check('+1 を 2 回で count=2', (await state()).count === 2, JSON.stringify(await state()));
	check('count=2 でリセットが押せる', !!(await hitCenter('count:reset')));

	// 文字欄: <input> をクリックして打ち、Enter
	const box = await c.evalJs(`(() => { const t = window.example.renderer.textInputs.find((x) => x.bindVar === 'name'); return t ? { x: t.x + t.w / 2, y: t.y + t.h / 2 } : null; })()`);
	check('文字欄の位置が出ている', !!box);
	if (box) {
		await c.click(box.x, box.y);
		await c.type('霧走');   // 1 字ずつ本物の打鍵（照合と同じ）
		await c.key('Enter');
		await sleep(150);
	}
	const s2 = await state();
	check('Enter で一覧に足される', s2.names.length === 1 && s2.names[0] === '霧走', JSON.stringify(s2));
	check('文字欄は空に戻る', (await c.evalJs(`document.querySelector('#stage input').value`)) === '');

	fs.mkdirSync(OUT, { recursive: true });
	fs.writeFileSync(path.join(OUT, 'counter.web.png'), await c.screenshot());

	await click('count:reset');
	check('リセットで count=0', (await state()).count === 0);
	check('リセット後はリセットが押せない', !(await hitCenter('count:reset')));
} catch (e) {
	fails++; console.log(`FAIL: ${e.message}`);
} finally {
	await c.close();
	srv.close();
}
console.log(fails ? `EXAMPLES FAIL checks=${checks} fail=${fails}` : `EXAMPLES PASS checks=${checks}`);
process.exit(fails ? 1 : 0);
