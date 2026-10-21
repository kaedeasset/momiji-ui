// momiji-ui.mjs — 宣言的 UI（UiNode の木＝画面 JSON）を Canvas2D で描く MomijiUI のブラウザ実装。**ここが源**（ES module）。
// 配布物（Tools/momiji-ui/web/dist/）と、<script> で読む従来形 Scripts/lib/momiji-ui-canvas.js は build.mjs がここから作る。
// 仕様: Docs/Guides/Working/momiji-ui-framework.md（UE が参照実装・ブラウザはそれに合わせる）。使い方: Docs/Guides/Manuals/momiji-ui-manual.md
//
// **描画式は UE の `Plugins/MomijiUI/Source/MomijiUISlate/Private/KaedeUiPainter.cpp` に合わせてある。**
//
// 一致させたもの（UE の該当箇所）:
//   角丸        kRadPanel 8 / kRadButton 10 / kRadModal 16 / kRadAtelier 6
//   行送り      kLineH 30・kLineHRatio 30/22（fontSize 指定時は比で算出）
//   既定px      KaedeLogicalFontSize 16（fontSize 未指定のノード）
//   縦センタ    字面の中心を箱の中心へ（KaedeInkCenteredTextPos）
//   テーマ      builtinTheme（UiNode.cpp）の atelier / midnight / sakura
//   レイアウト  MeasureStackH / VerticalLayout / HorizontalLayout / GridLayout / ScrollView / Modal
//   ボタン      EmitButtonStandard（atelier-primary / atelier-ghost）＋従来パス
//   スクロール条 幅6・右端-2・track 白6%・thumb #e0a04acc・thumbH=max(24, h*h/contentH)
//   設定部品    Slider / Toggle / ToggleGroup
//   前処理      evalCondExpr / seedVars / applyVars / setLabel / setActionEnabled / pruneTree / bindScreenTree（UE の host が描く前にやっていることの移植）
//   配置と描画  layout（描かずに描画命令・当たり・文字欄・スクロール領域を作る）→ paint（命令を Canvas2D へ）。2026-10-04
//
// **一致しないもの**: フォントのラスタライズ（Slate と Canvas2D で字送りが微妙に違う。仕様 §9）。
//
// このファイルの最上位は window / document に触らない（Node でも import できる＝診断 validate.mjs が使う）。


// ---- テーマ（UiNode.cpp builtinTheme("midnight")）----
const MIDNIGHT = {
  bg: 'rgba(14,17,22,1)', panel: 'rgba(24,31,43,1)', panelLight: 'rgba(34,43,58,1)',
  panelEdge: 'rgba(51,64,85,1)', accent: 'rgba(90,176,255,1)', accentDim: 'rgba(51,96,140,1)',
  track: 'rgba(9,12,18,0.902)', dim: 'rgba(0,0,0,0.627)',   // dim は UiTheme の既定 {0,0,0,160}
  textPrimary: 'rgba(230,237,246,1)', textDim: 'rgba(154,166,184,1)',
  textOnAccent: 'rgba(14,17,22,1)',
};

// builtinTheme（UiNode.cpp）の全種（F7・2026-10-03）。host は Renderer.setTheme(名前 | テーマ JSON) で選ぶ
const rgba = (r, g, b, a) => 'rgba(' + r + ',' + g + ',' + b + ',' + (a === undefined ? 1 : +(a / 255).toFixed(3)) + ')';
const ATELIER = {
  bg: rgba(0x1b, 0x14, 0x10), panel: rgba(0x2a, 0x21, 0x1a), panelLight: rgba(0x3a, 0x2e, 0x23),
  panelEdge: rgba(0x4a, 0x3a, 0x2b), accent: rgba(0xe0, 0xa0, 0x4a), accentDim: rgba(0x8a, 0x6a, 0x38),
  track: rgba(0x14, 0x0f, 0x0c, 230), dim: rgba(0, 0, 0, 160),
  textPrimary: rgba(0xf3, 0xe9, 0xd8), textDim: rgba(0xb8, 0xa9, 0x8f), textOnAccent: rgba(0x1b, 0x14, 0x10),
};
const SAKURA = Object.assign({}, ATELIER, {
  bg: rgba(0x2a, 0x1e, 0x25), panel: rgba(0x3b, 0x2a, 0x33), panelLight: rgba(0x4d, 0x37, 0x42),
  panelEdge: rgba(0x6a, 0x4a, 0x59), accent: rgba(0xff, 0x9d, 0xc0), accentDim: rgba(0xa8, 0x63, 0x7e),
  track: rgba(0x1d, 0x14, 0x19, 230), textPrimary: rgba(0xfa, 0xed, 0xf2), textDim: rgba(0xc9, 0xb0, 0xbb),
  textOnAccent: rgba(0x2a, 0x1e, 0x25),
});
const RAD = { panel: 8, button: 10, modal: 16, atelier: 6 };
const LINE_H = 30, LINE_H_RATIO = 30 / 22, DEF_PX = 16, CENTER_BIAS_EM = 0.25;
const SCROLLBAR_W = 10;   // 子幅から差し引く量（KaedeUiPainter の kScrollbarW）
const BAR_W = 6;          // 実際に描く条の幅（SKaedeUiCanvas）

const hexToCss = (s) => {
  if (typeof s !== 'string' || s[0] !== '#' || (s.length !== 7 && s.length !== 9)) return null;
  const h = (i) => parseInt(s.slice(i, i + 2), 16);
  const r = h(1), g = h(3), b = h(5), a = s.length === 9 ? h(7) : 255;
  return [r, g, b, a].some(Number.isNaN) ? null : 'rgba(' + r + ',' + g + ',' + b + ',' + (a / 255) + ')';
};

const lineHFor = (px) => (px > 0 ? Math.max(1, Math.round(px * LINE_H_RATIO)) : LINE_H);
const isBold = (font) => font === 'bold' || font === 'logo';
// 書体は UE の SKaedeUiCanvas が同梱して使う BIZ UDP ゴシック（2026-10-02）。ブラウザ版は Google Fonts から読む。
// 入っていなければ OS の游ゴシックへ落ちる（下描き HTML はそれで構わない）。
// **大きさは Slate と同じ換算にする。** UE の FSlateFontInfo の Size はポイント（96dpi で 1pt = 4/3 px）なので、
// 木の fontSize 17 は実際には 22.67px で描かれる（2026-10-03 に UE とブラウザの絵を並べて発覚。ブラウザだけ字が 3/4 だった）。
// 画素は整数に丸める（FreeType の文字の大きさは整数 px。22.67 で測った幅は 23 と一致する）。行送り（lineHFor）と箱の寸法は木の数値のまま＝UE と同じ。
const SLATE_PT_TO_PX = 96 / 72;
const fontCss = (bold, px) =>
  (bold ? '700 ' : '') + Math.round(px * SLATE_PT_TO_PX) + 'px "KaedeGothic","BIZ UDPGothic","Yu Gothic UI","Yu Gothic","Meiryo",sans-serif';

// 角丸矩形。半径0なら直角。
function roundRect(c, x, y, w, h, r) {
  const rr = Math.max(0, Math.min(r, Math.min(w, h) / 2));
  c.beginPath();
  if (rr <= 0) { c.rect(x, y, w, h); return; }
  c.moveTo(x + rr, y);
  c.arcTo(x + w, y, x + w, y + h, rr);
  c.arcTo(x + w, y + h, x, y + h, rr);
  c.arcTo(x, y + h, x, y, rr);
  c.arcTo(x, y, x + w, y, rr);
  c.closePath();
}

function Renderer(canvas) {
  this.c = canvas.getContext('2d');
  this.T = MIDNIGHT;
  this.vpW = 1280; this.vpH = 720;
  this.defPx = DEF_PX;
  this.hits = [];
  this.scrollAreas = [];
  this.scroll = new Map();   // id -> offset
  this._mw = new Map();      // 文字幅キャッシュ
  this._clip = [];           // 現在のスクロールid スタック
  this._band = null;         // いま見えている縦の範囲（スクロールの中だけ）
  // ---- ブラウザ版の本編（捜査ワークスペース）のため（2026-10-03）----
  this.images = new Map();   // sprite -> HTMLImageElement（読み終えたら onImageLoad で描き直してもらう）
  this.onImageLoad = null;
  this.onImageError = null;  // 読めない画像（sprite, url）。無ければ console.warn を画像ごとに 1 度
  this._imageWarned = new Set();
  this.textInputs = [];      // 描いた TextInput の位置（呼ぶ側が DOM の <input> を重ねる。IME を Canvas で受けないため）
  this.domTextInputs = false; // true なら TextInput の文字は描かない（重ねた <input> が出す）
  this.pulsing = false;      // 動いている部品がある（呼ぶ側が requestAnimationFrame で描き直し続ける）
  // ---- 動き（F5・UiMotionState の移植）。key = 画面 id + "/" + (id か action)。番号や座標には紐づけない ----
  this._screenId = '';       // host が画面ごとに screenId へ入れる（変わると画面全体が 160ms で浮かび、その画面の状態へ切り替わる。下の defineProperty）
  this.motion = new Map();   // key -> { t0, delay, spec, pulseT0, countFrom, countTo, countT0, last }
  this._screenT0 = 0;
  this._lastScreen = null;
  this._motionKey = '';      // いま描いている anim ノードのキー（入れ子の anim は外側が勝つ）
  this._groupKey = '';       // stagger の群（名前を持つ親）
  this._groupCount = new Map();
  this._seen = null;
  // ---- 焦点（F8・BuildFocusNav と SKaedeUiCanvas の焦点の枠の移植）----
  // 焦点は**部品の鍵**（id があれば "id:<id>"、無ければ "action:<action>"）で持つ（仕様 §7・2026-10-03）。
  // 描き直しても同じ鍵の部品へ戻す。鍵が曖昧（同じ action の部品が複数で id 無し）なら最前面の先頭へ落とす
  this._focusGroup = 0;      // いま描いている Modal の群（0 = Modal の外）
  this._modalSeq = 0;        // 描いた Modal の通し番号（群の番号。入れ子でも兄弟でも**後に描いた Modal が最前面**・仕様 §5.4・2026-10-04）
  this._clipRects = [];      // いま描いている ScrollView の見える範囲（画面座標）。文字欄の編集器のクリップに写す（F2・2026-10-04）
  this.focusKey = '';        // 焦点のある部品の鍵
  this.focusVisible = false; // 方向キーが来たら true、ポインタで false（UE と同じ）
  this._lastTop = 0;         // 前回描いたときの最前面の群（Modal の開閉を見る）
  this._focusStack = [];     // Modal を開く直前の焦点の鍵（閉じたら戻す）
  // ---- 画面ごとの状態（F3・2026-10-04・仕様 §6「画面の状態」）。スクロール量・焦点の鍵・控えた鍵・内部の下書きは**画面の名前（screenId）ごと**に持つ。
  // 画面が変わると、その画面の状態に切り替わる（別の画面の同じ id/bindVar と混ざらない）。戻れば前の状態。捨てるのは resetScreen / resetScreens
  this._screens = new Map(); // screenId -> { scroll: Map, focusKey, lastTop, focusStack, drafts: {} }
  this._screens.set(this._screenId, { scroll: this.scroll, focusKey: '', lastTop: 0, focusStack: this._focusStack, drafts: {} });
  this.cull = true;          // スクロールの見えない子の描画命令を作らない（配置と当たりは作る）。照合は false にして全部作る
  this._hitOnly = 0;         // >0 の間は配置と当たりだけ（描画命令を作らない）
  this._ops = [];            // 配置中に積む描画命令
  this.ops = [];             // 直近の配置の描画命令（paint が描く）
  this.recording = false;    // true なら配置のあと prims（照合に記録する命令: 矩形・文字・画像…）を作る
  this.prims = [];
  this._noRecord = 0;        // スクロール条・焦点の枠など、UE が描画命令に持たないもの（chrome）を記録しない
  this._textUnscale = null;  // 脈動中の文字を元の大きさ・位置に戻す逆変換（nodeAnim が立て、_op が文字の op に差し込む）
  this.imageSource = null;   // sprite（画像の鍵）→ URL。host が差し替える（無ければ鍵をそのまま URL に）
  // ---- 押下フィードバック（SKaedeUiCanvas::OnPaint と同じ: 押している間 0.96 倍＋色 0.85・ホバーで白 10%（120ms で）・離した瞬間 白 22% が 150ms で消える）----
  this.hoverKey = ''; this.hoverT0 = 0;
  this.pressedKey = '';
  this.flashKey = ''; this.flashT0 = -1;
  // ---- 文字の選択（2026-10-07・設計 Docs/Reports/2026/10/07/01・UE の SKaedeUiCanvas と同じ規則）----
  // 配置が selectable の器（selAreas）と、その中の Label の行（selLines）を集める。スクロールで見えない行も集める（行の番号を見え方に依らせない）
  this.selLines = [];        // { group, run, text, x, y, lineH, bold, px, scrollId }（y はスクロール前の座標）
  this.selAreas = [];        // { group, x, y, w, h, scrollId, focusGroup }
  this._selGroupLines = new Map();   // 器の鍵 → selLines の番号（描画順）
  this._selGroup = ''; this._selRunSeq = 0; this._selOrd = 0; this._selRun = -1;
  this._sel = null;          // { group, anchor: {line, ch}, focus: {line, ch}, firstLine, texts }
  this.copiedAt = -1;        // 「コピーしました」の札（900ms）
  this.copiedPos = null;
  this._host = null;         // attach の受け口（描く前と後に呼ぶ。attach が入れ、detach が外す）
}
// 画面の名前。入れた瞬間にその画面の状態（スクロール量・焦点・控えた鍵・内部の下書き）へ切り替わる（F3）。描いたときの 160ms の浮かびは draw が見る
Object.defineProperty(Renderer.prototype, 'screenId', {
  get() { return this._screenId; },
  set(v) { const id = v === undefined || v === null ? '' : String(v); if (id === this._screenId) return; this._switchScreen(this._screenId, id); this._screenId = id; },
});
// 部品の鍵（仕様 §7）
function hitKey(h) { return h.id ? 'id:' + h.id : 'action:' + (h.action || ''); }
Object.defineProperty(Renderer.prototype, 'focusAction', {
  get() { const h = this.focusedHit(); return h ? h.action : ''; },   // 互換（鍵ではなく action を読む旧い host 用）
});
// 名前（atelier / midnight / sakura）かテーマ JSON（{bg, panel, …: "#rrggbb[aa]"}）。知らない名前は false
Renderer.prototype.setTheme = function (t) {
  if (typeof t === 'string') {
    const base = t === 'midnight' ? MIDNIGHT : t === 'sakura' ? SAKURA : (t === 'atelier' || t === '') ? ATELIER : null;
    if (!base) return false;
    this.T = Object.assign({}, base);
    return true;
  }
  if (t && typeof t === 'object') {
    const out = Object.assign({}, this.T);
    for (const k of Object.keys(out)) { const v = hexToCss(t[k]); if (v) out[k] = v; }
    this.T = out;
    return true;
  }
  return false;
};

Renderer.prototype.measure = function (s, bold, px) {
  const p = px > 0 ? px : this.defPx;
  const key = p + (bold ? 'b' : 'r') + s;
  if (this._mw.has(key)) return this._mw.get(key);
  this.c.font = fontCss(bold, p);
  const w = this.c.measureText(s).width;
  if (this._mw.size < 8192) this._mw.set(key, w);
  return w;
};

// KaedeUiPainter::EllipsizeToWidth 相当。入らなければ末尾を落として「…」を付ける。
Renderer.prototype.ellipsize = function (s, w, bold, px) {
  if (this.measure(s, bold, px) <= w) return s;
  const dots = '…';
  let lo = 0, hi = s.length;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (this.measure(s.slice(0, mid) + dots, bold, px) <= w) lo = mid; else hi = mid - 1;
  }
  return lo <= 0 ? dots : s.slice(0, lo) + dots;
};

// KaedeUiPainter::WrapToLines の移植。空白があれば単語境界、無ければ文字境界。
Renderer.prototype.wrap = function (text, wrapW, bold, px) {
  const lines = [];
  let cur = '', lastBreak = -1;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch === '\n') { lines.push(cur); cur = ''; lastBreak = -1; continue; }
    cur += ch;
    if (ch === ' ') lastBreak = cur.length;
    if (cur.length > 1 && this.measure(cur, bold, px) > wrapW) {
      if (lastBreak > 0 && lastBreak < cur.length) {
        lines.push(cur.slice(0, lastBreak).replace(/\s+$/, ''));
        cur = cur.slice(lastBreak);
      } else {
        const last = cur[cur.length - 1];
        cur = cur.slice(0, -1);
        lines.push(cur);
        cur = last;
      }
      lastBreak = -1;
    }
  }
  if (cur.length) lines.push(cur);
  return lines.length ? lines : [text];
};

// ---- 配置の結果＝描画命令（ops）。配置（layout）は描かず命令を積み、描画（paint）が命令を実行する（2026-10-04・OSS 計画 §10.3）----
// 命令は論理座標。種類: rect / outline / text / lines / ellipse / image / push・pop（動き・押下の変換）/ clip・unclip（ScrollView・cover）。
// chrome: true はキャンバスの飾り（スクロール条・押下の光り・焦点の枠）＝UE の描画命令に無いので照合の prims に入れない
Renderer.prototype._op = function (o) {
  o.chrome = this._noRecord > 0;
  const u = this._textUnscale;
  if (u && o.op === 'text') {   // 脈動中の文字: 箱の拡大を打ち消して元の大きさ・位置で描く（nodeAnim 参照）
    this._ops.push({ op: 'push', alpha: 1, tx: 0, ty: 0, scale: u.scale, cx: u.cx, cy: u.cy, chrome: o.chrome });
    this._ops.push(o);
    this._ops.push({ op: 'pop', chrome: o.chrome });
    return;
  }
  this._ops.push(o);
};
Renderer.prototype.fill = function (x, y, w, h, css, rad) {
  if (this._hitOnly) return;
  this._op({ op: 'rect', x: x, y: y, w: w, h: h, css: css, rad: rad || 0 });
};
Renderer.prototype.outline = function (x, y, w, h, css, rad) {
  if (this._hitOnly) return;
  this._op({ op: 'outline', x: x, y: y, w: w, h: h, css: css, rad: rad || 0 });
};

// KaedeUiPainter::EmitPolyline。事件の図が線を引くのに使う（これが無くて図だけ SVG で描いていた）。
Renderer.prototype.polyline = function (pts, css, thickness, closed) {
  if (!pts || pts.length < 2 || this._hitOnly) return;
  this._op({ op: 'lines', pts: pts, css: css, thickness: thickness || 1, closed: !!closed });
};

// KaedeUiPainter::EmitLabel。wrapW>0 は左寄せ上詰めで複数行、それ以外は1行。
// 文字の命令は描く位置（x, y, baseline）と、照合に記録する位置（px, py＝送り幅で寄せた位置。UE の描画命令の X・Y と同じ）を持つ
Renderer.prototype.label = function (s, x, y, w, h, css, align, centerFallback, font, wrapW, px, lineH, ellip) {
  // 選べる Label の行（selRun >= 0）は、スクロールで見えず描かない（_hitOnly）ときも行として集める（文字の選択・2026-10-07）
  const sel = this._selRun >= 0;
  if (!s || (this._hitOnly && !sel)) return;
  const bold = isBold(font);
  const p = px > 0 ? px : this.defPx;
  const fontStr = fontCss(bold, p);
  const selOf = (lh) => (sel ? { selGroup: this._selGroup, selRun: this._selRun, lineH: lh } : {});
  if (wrapW > 0) {
    const lines = this.wrap(s, wrapW, bold, px);
    const lh = lineH > 0 ? lineH : lineHFor(px);
    for (let i = 0; i < lines.length; i++) {
      if (sel) this._selLine(lines[i], x, y + i * lh, lh, bold, px);
      if (!this._hitOnly) this._op(Object.assign({ op: 'text', text: lines[i], x: x, y: y + i * lh, baseline: 'top', font: fontStr, css: css, px: x, py: y + i * lh }, selOf(lh)));
    }
    return;
  }
  let text = s;
  if (ellip && w > 0) text = this.ellipsize(text, w, bold, px);
  const center = align === 'center' || (!align && centerFallback);
  const right = align === 'right';
  const tw = this.measure(text, bold, px);
  const primX = center ? x + (w - tw) / 2 : right ? x + w - tw : x;
  if (sel) this._selLine(text, primX, y, h, bold, px);   // 1 行の Label は箱の高さが行（UE の LineH = 箱の h）
  if (this._hitOnly) return;
  // 縦は字面（インクが乗る範囲）を箱の中心へ。中央寄せは横も字面で（UE の KaedeInkCenteredTextPos と同じ考え・2026-10-02）
  const c = this.c;
  c.font = fontStr;
  const m = c.measureText(text);
  const ascent = m.actualBoundingBoxAscent, descent = m.actualBoundingBoxDescent;
  const inkOk = Number.isFinite(ascent) && Number.isFinite(descent) && ascent + descent > 0;
  let tx = primX;
  if (center && inkOk && Number.isFinite(m.actualBoundingBoxLeft) && Number.isFinite(m.actualBoundingBoxRight)) {
    tx = x + (w - (m.actualBoundingBoxLeft + m.actualBoundingBoxRight)) / 2 + m.actualBoundingBoxLeft;
  }
  if (inkOk) this._op(Object.assign({ op: 'text', text: text, x: tx, y: y + h / 2 + (ascent - descent) / 2, baseline: 'alphabetic', font: fontStr, css: css, px: primX, py: y }, selOf(h)));
  // 字面が測れない（空白だけ等）。旧式: 箱の中央から 0.25em 上（KaedeTextCenterBiasEm）
  else this._op(Object.assign({ op: 'text', text: text, x: tx, y: y + h / 2 - CENTER_BIAS_EM * p * 0.5, baseline: 'middle', font: fontStr, css: css, px: primX, py: y }, selOf(h)));
};
// 選べる行を 1 つ集める（文字の選択）。座標はスクロール前、scrollId はいちばん内側の ScrollView
Renderer.prototype._selLine = function (text, x, y, lineH, bold, px) {
  this.selLines.push({ group: this._selGroup, run: this._selRun, text: text, x: x, y: y, lineH: lineH, bold: bold, px: px || 0,
    scrollId: this._scrollId() });
};

// runs（区間ごとの色・太字・ルビ）。layoutRuns の移植: ルビ付きは割らず 1 片、ほかは 1 字ずつ、wrapW で折り返す
Renderer.prototype.runs = function (n, x, y, w, baseCss) {
  const sel = this._selRun >= 0;   // 選べる片は描かないときも集める（ルビ片は選べない）
  if (this._hitOnly && !sel) return;
  const wrapW = n.wrapW > 0 ? n.wrapW : (w > 0 ? w : 400);
  const hasRuby = n.runs.some((r) => r && r.ruby);
  const rubyHead = hasRuby ? 16 : 0;
  const basePx = n.fontSize || 0, rubyPx = basePx > 0 ? basePx * 0.5 : 11;
  const lineH = lineHFor(basePx);
  const right = x + wrapW;
  let cx = x, cy = y;
  const place = (t, r, keepRuby) => {
    const tw = Math.round(this.measure(t, !!r.bold, basePx));
    if (cx > x && cx + tw > right) { cx = x; cy += lineH; }
    const css = hexToCss(r.color) || baseCss;
    if (sel) this._selLine(t, cx, cy + rubyHead, lineH, !!r.bold, basePx);
    if (!this._hitOnly) this._op(Object.assign({ op: 'text', text: t, x: cx, y: cy + rubyHead, baseline: 'top', font: fontCss(!!r.bold, basePx > 0 ? basePx : this.defPx), css: css, px: cx, py: cy + rubyHead },
      sel ? { selGroup: this._selGroup, selRun: this._selRun, lineH: lineH } : {}));
    if (keepRuby && r.ruby && !this._hitOnly) {
      const rw = this.measure(r.ruby, false, rubyPx);
      this._op({ op: 'text', text: r.ruby, x: cx + tw / 2 - rw / 2, y: cy, baseline: 'top', font: fontCss(false, rubyPx), css: css, px: cx + tw / 2 - rw / 2, py: cy });
    }
    cx += tw;
  };
  for (const r of n.runs) {
    if (!r || typeof r.text !== 'string') continue;
    if (r.ruby) place(r.text, r, true);
    else for (const ch of r.text) place(ch, r, false);
  }
};

// ---- 焦点（BuildFocusNav の移植）: 同じ群の中で、各方向に中心が正しい側にある最も近いもの（横ずれは 2 倍で罰する）
Renderer.prototype.focusIndex = function () {
  if (!this.focusKey) return -1;
  return this.hits.findIndex((h) => h.focusable && h.key === this.focusKey);
};
// 最前面の群。**押せない当たり（Modal の暗幕）も数える**＝操作対象の無い Modal でも背景へ入力が漏れない
Renderer.prototype.topFocusGroup = function () {
  let g = 0; for (const h of this.hits) if (h.group > g) g = h.group; return g;
};
Renderer.prototype.resetFocus = function () {
  const g = this.topFocusGroup();
  const h = this.hits.find((x) => x.focusable && x.group === g);
  this.focusKey = h ? h.key : '';
};
// 鍵の部品を群の中で探す。一つだけなら番号、無ければ -1、複数（曖昧）なら -2
Renderer.prototype.findKey = function (key, group) {
  let found = -1, count = 0;
  this.hits.forEach((h, i) => { if (h.focusable && h.group === group && h.key === key) { if (found < 0) found = i; count++; } });
  return count === 1 ? found : (count > 1 ? -2 : -1);
};
// 描き直すたびに焦点を整える（SKaedeUiCanvas::ReconcileFocus と同じ規則・仕様 §7）:
//   群が増えた（Modal が開いた）→ いまの鍵を控えて最前面の先頭へ／減った（閉じた）→ 控えた鍵へ戻す（無ければ先頭）／
//   同じ → 同じ鍵が最前面に一つだけあれば保持、無い・曖昧なら先頭
Renderer.prototype.reconcileFocus = function (prevKey, prevTop) {
  const top = this.topFocusGroup();
  if (top > prevTop) {
    if (prevKey) this._focusStack.push(prevKey);
    this.resetFocus();
  } else if (top < prevTop) {
    const back = this._focusStack.length ? this._focusStack.pop() : '';
    const i = back ? this.findKey(back, top) : -1;
    if (i >= 0) this.focusKey = this.hits[i].key; else this.resetFocus();
  } else {
    const i = prevKey ? this.findKey(prevKey, top) : -1;
    if (i >= 0) this.focusKey = this.hits[i].key; else this.resetFocus();
  }
  this._lastTop = top;
};
// i 番目の当たりから (dx, dy) 方向へ移る先の番号（-1 = 無し）。BuildFocusNav と同じ式（中心の比較・横ずれ 2 倍）
Renderer.prototype.focusNavFrom = function (i, dx, dy) {
  const H = this.hits, me = H[i];
  if (!me || !me.focusable) return -1;
  const cxOf = (h) => h.x + h.w / 2, cyOf = (h) => h.y + h.h / 2;
  const ix = cxOf(me), iy = cyOf(me);
  let best = -1, bestS = Infinity;
  for (let j = 0; j < H.length; j++) {
    if (j === i || !H[j].focusable || H[j].group !== me.group) continue;
    const ddx = cxOf(H[j]) - ix, ddy = cyOf(H[j]) - iy;
    let s = Infinity;
    if (dy < 0 && ddy < -1) s = -ddy + Math.abs(ddx) * 2;
    if (dy > 0 && ddy > 1) s = ddy + Math.abs(ddx) * 2;
    if (dx < 0 && ddx < -1) s = -ddx + Math.abs(ddy) * 2;
    if (dx > 0 && ddx > 1) s = ddx + Math.abs(ddy) * 2;
    if (s < bestS) { bestS = s; best = j; }
  }
  return best;
};
// dx/dy は -1/0/1。動けたら true（焦点の枠を出す）。移った先がスクロールの外なら見える位置まで寄せる
Renderer.prototype.moveFocus = function (dx, dy) {
  this.focusVisible = true;
  const i = this.focusIndex();
  if (i < 0 || this.hits[i].group !== this.topFocusGroup()) { this.resetFocus(); this.ensureScrollVisible(this.focusedHit()); return this.focusIndex() >= 0; }
  const best = this.focusNavFrom(i, dx, dy);
  if (best < 0) return false;
  this.focusKey = this.hits[best].key;
  this.ensureScrollVisible(this.hits[best]);
  return true;
};
// 当たりがスクロールの見える範囲に入るようスクロール量を動かす（SKaedeUiCanvas::EnsureScrollVisible と同じ式）
Renderer.prototype.ensureScrollVisible = function (h) {
  if (!h || !h.scrollId) return;
  const a = this.scrollAreas.find((x) => x.id === h.scrollId);
  if (!a) return;
  let off = this.scroll.get(h.scrollId) || 0;
  const top = h.y - off, bottom = h.y + h.h - off;
  if (top < a.y) off -= (a.y - top);
  else if (bottom > a.y + a.h) off += (bottom - (a.y + a.h));
  this.scroll.set(h.scrollId, Math.max(0, Math.min(off, Math.max(0, a.contentH - a.h))));
};
Renderer.prototype.focusedHit = function () { const i = this.focusIndex(); return i >= 0 ? this.hits[i] : null; };

// ---- 画面ごとの状態（F3・仕様 §6「画面の状態」）----
// 画面の名前の状態（無ければ作る）。id 省略はいまの画面
Renderer.prototype.screenState = function (id) {
  const key = id === undefined ? this._screenId : id;
  let s = this._screens.get(key);
  if (!s) { s = { scroll: new Map(), focusKey: '', lastTop: 0, focusStack: [], drafts: {} }; this._screens.set(key, s); }
  return s;
};
// いまの画面の状態を控え、screenId の画面の状態へ切り替える（draw が画面の名前の変化を見て呼ぶ）
Renderer.prototype._switchScreen = function (from, to) {
  this._sel = null;   // 選択は画面をまたがない（§4.4）
  const old = this.screenState(from);
  old.focusKey = this.focusKey; old.lastTop = this._lastTop;   // scroll と focusStack は同じ実体を指している
  const s = this.screenState(to);
  this.scroll = s.scroll; this._focusStack = s.focusStack; this.focusKey = s.focusKey; this._lastTop = s.lastTop;
};
// 画面の状態を捨てる（画面を作り直す・はじめからやり直す）。id 省略はいまの画面。いまの画面なら空の状態になる
Renderer.prototype.resetScreen = function (id) {
  const key = id === undefined ? this._screenId : id;
  this._screens.delete(key);
  if (key === this._screenId) this._sel = null;
  if (key === this._screenId) { const s = this.screenState(key); this.scroll = s.scroll; this._focusStack = s.focusStack; this.focusKey = ''; this._lastTop = 0; }
};
Renderer.prototype.resetScreens = function () { this._screens.clear(); this.resetScreen(this._screenId); };
// 文字欄が**いま操作できる**か（仕様 §6「操作層」・F1/F2）: 押せて、最前面の群にあり、見える部分がある。
// 編集器（<input> / SEditableText）はこれが偽の欄に焦点を置かず、Enter も送らない
Renderer.prototype.inputInteractive = function (t) {
  return !!t && t.enabled && t.group === this.topFocusGroup() && !!t.clip && t.clip.w > 0 && t.clip.h > 0;
};

Renderer.prototype.addHit = function (n, x, y, w, h) {
  if (n.enabled === false || !n.action) return null;
  const hit = { x: x, y: y, w: w, h: h, action: n.action, id: n.id || '', type: n.type || 'Panel',
                text: n.text || '', scrollId: this._scrollId(),
                focusable: n.focusable !== false, group: this._focusGroup, on: !!n.on };
  hit.key = hitKey(hit);
  this.hits.push(hit);
  return hit;
};

// KaedeUiPainter::MeasureStackH。h>0 ならそれ。無ければ中身から。既定 28。
// 折り返す Label の高さを測るための計測（F1・2026-10-03）。draw の間だけ立つ（UE は MeasureStackH の引数で渡す）
let stackRenderer = null;
function measureStackH(n, measurer) {
  const m = measurer || stackRenderer;
  if (n.h > 0) return n.h;
  const pad = n.pad || 0, gap = n.gap === undefined ? 8 : n.gap;
  const kids = n.children || [];
  switch (n.type) {
    case 'Label': {
      // 折り返す Label（wrapW>0・h 無し）は描くときと同じ折り返しで行数を数えて高さにする（F1）
      if (n.wrapW > 0 && m && n.text) {
        const lines = m.wrap(n.text, n.wrapW, isBold(n.font || 'bold'), n.fontSize || 0).length;
        return Math.max(1, lines) * (n.lineH > 0 ? n.lineH : lineHFor(n.fontSize || 0));
      }
      return 28;
    }
    case 'VerticalLayout': {
      let h = 2 * pad;
      for (const c of kids) h += 2 * (c.margin || 0) + measureStackH(c, m) + gap;
      if (kids.length) h -= gap;
      return Math.max(h, 0);
    }
    case 'HorizontalLayout': {
      let maxH = 0;
      for (const c of kids) maxH = Math.max(maxH, 2 * (c.margin || 0) + measureStackH(c, m));
      return 2 * pad + maxH;
    }
    case 'GridLayout': {
      // cols 列で折り返した行数 × 行高（子の最大高さ）
      if (!kids.length) return 2 * pad;
      const cols = Math.max(1, n.cols || 1);
      let rowH = 0;
      for (const c of kids) rowH = Math.max(rowH, 2 * (c.margin || 0) + measureStackH(c, m));
      const rows = Math.ceil(kids.length / cols);
      return 2 * pad + rows * rowH + (rows - 1) * gap;
    }
    default: return 28;
  }
}

// Component Standard のコンテナロール（atelier ファミリのみ・pc98 は未移植）
const containerRole = (v) =>
  v === 'atelier-surface' ? 'surface' : v === 'atelier-card' ? 'card'
  : v === 'atelier-icon-tile' ? 'tile' : '';

// highlight の背景明化（HighlightFill: accent へ 18%）
function mixAccent(cssBase, cssAccent) {
  const p = (s) => (s.match(/[\d.]+/g) || []).map(Number);
  const a = p(cssBase), b = p(cssAccent);
  if (a.length < 3 || b.length < 3) return cssBase;
  const m = (i) => Math.round(a[i] + (b[i] - a[i]) * 0.18);
  return 'rgba(' + m(0) + ',' + m(1) + ',' + m(2) + ',' + (a[3] === undefined ? 1 : a[3]) + ')';
}

// EmitNode（UIモーションの包み）の移植。anim 付きノードに安定キーを振り、出現・数の増減・脈動を描く
const ease = {
  easeOutQuad: (t) => 1 - (1 - t) * (1 - t),
  easeOutCubic: (t) => 1 - Math.pow(1 - t, 3),
  easeInOutQuad: (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2),
  linear: (t) => t,
};
const easeOf = (name, fallback) => ease[name] || ease[fallback];
const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const NUM_RE = /^-?[\d,]+(\.\d+)?$/;
function formatCount(v, grouped, isInt) {
  let s = isInt ? String(Math.round(v)) : String(Math.round(v * 1000) / 1000);
  if (grouped) s = s.replace(/^(-?)(\d+)/, (m, sign, d) => sign + d.replace(/\B(?=(\d{3})+(?!\d))/g, ','));
  return s;
}
Renderer.prototype.node = function (n) {
  if (!n) return;
  const name = n.id || n.action || '';
  let key = '';
  if (n.anim && !this._motionKey && name) { key = this.screenId + '/' + name; this._motionKey = key; }
  const savedGroup = this._groupKey;
  if (name) this._groupKey = this.screenId + '/' + name;
  if (!key) { this.nodeBody(n); this._groupKey = savedGroup; return; }

  const now = performance.now();
  const a = n.anim || {};
  const en = a.enter && a.enter.type ? a.enter : null, cu = a.countUp || null, pu = a.pulse || null;   // enter は type が空なら無い（UiAnimSpec::hasEnter）
  let e = this.motion.get(key);
  const textNum = typeof n.text === 'string' && NUM_RE.test(n.text) ? Number(n.text.replace(/,/g, '')) : null;
  if (!e) {
    e = { t0: now, delay: 0, pulseT0: now, countT0: -1, countFrom: 0, countTo: 0, last: textNum };
    if (en) {
      const rank = this._groupCount.get(savedGroup) || 0;
      this._groupCount.set(savedGroup, rank + 1);
      e.delay = (en.delayMs || 0) + (a.staggerMs || 0) * rank;
    }
    this.motion.set(key, e);
  } else if (cu && textNum !== null && e.last !== null && textNum !== e.last) {
    e.countFrom = e.last; e.countTo = textNum; e.countT0 = now;   // 値が変わったら数え上げる（初回は即時）
  }
  if (textNum !== null) e.last = textNum;
  if (this._seen) this._seen.add(key);

  let alpha = 1, offX = 0, offY = 0, scale = 1, text = null, enterScale = 1;
  if (en) {
    const dur = en.durationMs > 0 ? en.durationMs : 220;
    const raw = (now - e.t0 - e.delay) / dur;
    if (raw < 1) {
      this.pulsing = true;
      const type = en.type;
      const p = easeOf(en.easing, type === 'fade' ? 'easeOutQuad' : 'easeOutCubic')(clamp01(raw));
      alpha = p;
      if (type === 'slide') {
        const d = (en.distance > 0 ? en.distance : 40) * (1 - p), from = en.from || 'bottom';
        if (from === 'left') offX = -d; else if (from === 'right') offX = d; else if (from === 'top') offY = -d; else offY = d;
      } else if (type === 'pop') {
        scale = 0.85 + 0.15 * p; enterScale = scale;
      }
    }
  }
  if (cu && e.countT0 >= 0) {
    const dur = cu.durationMs > 0 ? cu.durationMs : 400;
    const raw = (now - e.countT0) / dur;
    if (raw < 1) {
      this.pulsing = true;
      const p = easeOf(cu.easing, 'easeOutQuad')(clamp01(raw));
      const isInt = Math.floor(e.countFrom) === e.countFrom && Math.floor(e.countTo) === e.countTo;
      text = formatCount(e.countFrom + (e.countTo - e.countFrom) * p, n.text.includes(','), isInt);
    } else e.countT0 = -1;
  }
  const pulseOn = pu && n._pulseOn !== false;
  e.pulseOn = !!pulseOn;   // いま脈動しているか（host や検証が読む）
  if (pulseOn) {
    this.pulsing = true;
    const period = pu.periodMs > 0 ? pu.periodMs : 900;
    const wave = 0.5 * (1 - Math.cos(((now - e.pulseT0) / period) * 2 * Math.PI));
    scale *= 1 + ((pu.scale > 0 ? pu.scale : 1.06) - 1) * wave;
  } else e.pulseT0 = now;   // 位相を戻す（次の作動は 1.0 から）

  const cx = (n.x || 0) + (n.w || 0) / 2, cy = (n.y || 0) + (n.h || 0) / 2;
  const moving = alpha !== 1 || offX || offY || scale !== 1;
  if (moving && !this._hitOnly) this._op({ op: 'push', alpha: alpha, tx: offX, ty: offY, scale: scale, cx: cx, cy: cy });
  // 脈動は箱だけ。文字は大きさも位置も変えない（UE の UiMotionSample.textFixed と同じ規則。enter の pop と重なる間は全体）。
  // 文字の op だけ同じ中心で 1/scale を掛けて戻す（_op が差し込む）
  const savedUnscale = this._textUnscale;
  this._textUnscale = (moving && !this._hitOnly && pulseOn && enterScale === 1 && scale !== 1) ? { scale: 1 / scale, cx: cx, cy: cy } : null;
  this.nodeBody(text !== null ? Object.assign({}, n, { text }) : n);
  this._textUnscale = savedUnscale;
  if (moving && !this._hitOnly) this._op({ op: 'pop' });
  this._motionKey = '';
  this._groupKey = savedGroup;
};

// 押下フィードバック: 押せる部品（action あり）に、押している間の縮小と、ホバー・離した瞬間の明るさを重ねる
Renderer.prototype.nodeBody = function (n) {
  if (!n || this._hitOnly || !n.action || n.enabled === false || n.type === 'Modal') { this.nodeBodyInner(n); return; }
  const key = hitKey(n);
  const now = performance.now();
  let scale = 1, dark = 0, white = 0;
  if (this.pressedKey && key === this.pressedKey) { scale = 0.96; dark = 0.15; }
  else if (!this.focusVisible && this.hoverKey && key === this.hoverKey) {
    const t = Math.min(1, (now - this.hoverT0) / 120);
    white = 0.10 * t;
    if (t < 1) this.pulsing = true;
  }
  if (this.flashT0 >= 0 && this.flashKey && key === this.flashKey) {
    const t = (now - this.flashT0) / 150;
    if (t < 1) { white = Math.max(white, 0.22 * (1 - t)); this.pulsing = true; }
  }
  if (scale === 1 && !dark && !white) { this.nodeBodyInner(n); return; }
  const x = n.x || 0, y = n.y || 0, w = n.w || 0, h = n.h || 0;
  const cx = x + w / 2, cy = y + h / 2;
  if (scale !== 1) this._op({ op: 'push', alpha: 1, tx: 0, ty: 0, scale: scale, cx: cx, cy: cy });
  this.nodeBodyInner(n);
  if ((dark || white) && w > 0 && h > 0) {
    this._noRecord++;   // 押下の光りはキャンバスの飾り（照合の prims に入れない）
    const rad = n.type === 'Button' ? ((n.variant === 'atelier-primary' || n.variant === 'atelier-ghost') ? RAD.atelier : RAD.button) : RAD.panel;
    this.fill(x, y, w, h, dark ? 'rgba(0,0,0,' + dark + ')' : 'rgba(255,255,255,' + white.toFixed(3) + ')', rad);
    this._noRecord--;
  }
  if (scale !== 1) this._op({ op: 'pop' });
};
const SEL_TYPES = new Set(['Panel', 'VerticalLayout', 'ScrollView']);
const colOf = (v, fb) => (hexToCss(v) || fb);
// 入れ物（ScrollView・縦横・格子）が地を塗るか: 色・強調・light / dark・役割の variant があるときだけ
const paintsContainer = (n) => !!(n.color || n.highlight || n.variant === 'light' || n.variant === 'dark' || containerRole(n.variant));
// いま描いているスクロールの id（スクロールの外なら空）
Renderer.prototype._scrollId = function () { return this._clip.length ? this._clip[this._clip.length - 1] : ''; };
// いま描いている ScrollView の見える範囲（画面座標。外なら画面全体）
Renderer.prototype._clipRect = function () { return this._clipRects.length ? this._clipRects[this._clipRects.length - 1] : { x: 0, y: 0, w: this.vpW, h: this.vpH }; };
Renderer.prototype.nodeBodyInner = function (n) {
  if (!n) return;
  const type = n.type || 'Panel';
  const x = n.x || 0, y = n.y || 0, w = n.w || 0, h = n.h || 0;
  // 文字の選択の器（2026-10-07・設計 10/07/01 §3・§4.1）。器の矩形を控え、中の Label の行に器の鍵を写す。入れ子なら内側の器が勝つ。
  // 器の矩形は親の座標（ScrollView なら自分のスクロールの外）。UE の EmitNodeBody（SelectArea）と同じ順に番号（ord）を振る
  if (n.selectable === true && !n._selIn && SEL_TYPES.has(type)) {
    const saved = [this._selGroup, this._selRunSeq, this._selRun];
    this._selGroup = n.id ? hitKey(n) : 'ord:' + (this._selOrd++);
    this._selRunSeq = 0; this._selRun = -1;
    this.selAreas.push({ group: this._selGroup, x: x, y: y, w: w, h: h, scrollId: this._scrollId(), focusGroup: this._focusGroup });
    try { this.nodeBodyInner(Object.assign({}, n, { _selIn: true })); }
    finally { this._selGroup = saved[0]; this._selRunSeq = saved[1]; this._selRun = saved[2]; }
    return;
  }
  const kids = n.children || [];

  // **見えていないものは描かない。** スクロールの中は中身が数千pxあるのに、
  // 見えているのは 460px だけ。全部描くと押すたびに数千の札を測り直すことになる
  //（2026-09-04: 筋道 5471px で重かった）。UE の Canvas も同じことをする。
  // ただし**配置と当たりは作る**（2026-10-03）——方向キーで見えない次の項目へ移れるように（UE は当たりを持ち、
  // 押されたときに見える範囲で外す。ブラウザも同じ）。描く原始関数は _hitOnly の間は何もしない
  if (this._band && this.cull && !this._hitOnly) {
    const b = this._band;
    let off = false;
    if (type === 'Polyline') {
      const pts = n.points || [];
      let mn = Infinity, mx = -Infinity;
      for (let i = 0; i < pts.length; i++) { const v = pts[i][1]; if (v < mn) mn = v; if (v > mx) mx = v; }
      off = pts.length > 0 && (mx < b.top || mn > b.bottom);
    } else if (h > 0 && (y + h < b.top || y > b.bottom)) off = true;
    if (off) {
      if (!n.action && !kids.length && !(this._selGroup && type === 'Label')) return;   // 押せない・子も無い → 何も要らない（選べる Label は行を集める）
      this._hitOnly++;
      try { this.nodeBody(n); } finally { this._hitOnly--; }
      return;
    }
  }

  const box = { x: x, y: y, w: w, h: h, pad: n.pad || 0, gap: n.gap === undefined ? 8 : n.gap,
                enabled: n.enabled !== false, kids: kids, px: n.fontSize || 0 };
  (Object.prototype.hasOwnProperty.call(NODE_PAINT, type) ? NODE_PAINT[type] : nodePaintDefault).call(this, n, box);
};

// コンテナの面（KaedeUiPainter の共通式）
Renderer.prototype._paintContainer = function (n, x, y, w, h, rad) {
  const T = this.T;
  const role = containerRole(n.variant);
  if (role) {
    const base = role === 'card' ? T.panelLight : role === 'tile' ? T.track : T.panel;
    this.fill(x, y, w, h, n.highlight ? mixAccent(base, T.accent) : base, RAD.atelier);
    this.outline(x, y, w, h, n.highlight ? T.accent : T.panelEdge, RAD.atelier);
    return;
  }
  const base = n.color ? colOf(n.color) : n.variant === 'light' ? T.panelLight : n.variant === 'dark' ? T.track : T.panel;
  this.fill(x, y, w, h, n.highlight ? mixAccent(base, T.accent) : base, rad);
  this.outline(x, y, w, h, n.highlight ? T.accent : T.panelEdge, rad);
};

function nodePaintDefault(n, b) {
  this.fill(b.x, b.y, b.w, b.h, this.T.panel, 0);
  for (const k of b.kids) this.node(k);
}

// 部品の種類ごとの配置と描画（nodeBodyInner が呼ぶ。this は Renderer・b は { x, y, w, h, pad, gap, enabled, kids, px }）
const NODE_PAINT = {
  Panel(n, b) {
    if (n.variant !== 'spacer') this._paintContainer(n, b.x, b.y, b.w, b.h, RAD.panel);
    if (n.variant !== 'spacer') this.addHit(n, b.x, b.y, b.w, b.h);
    for (const k of b.kids) this.node(k);
  },
  Label(n, b) {
    const T = this.T;
    // selectable の器の中なら、この Label の行は選べる（Label ごとに番号を 1 つ・UE と同じ）
    const savedRun = this._selRun;
    if (this._selGroup) this._selRun = this._selRunSeq++;
    if (Array.isArray(n.runs) && n.runs.length) {   // 区間ごとの色・太字・ルビ（EmitRuns / layoutRuns の移植）
      this.runs(n, b.x, b.y, b.w, n.textColor ? colOf(n.textColor, T.textPrimary) : (b.enabled ? T.textPrimary : T.textDim));
    } else {
      this.label(n.text || '', b.x, b.y, b.w > 0 ? b.w : 400, b.h > 0 ? b.h : 28,
        n.textColor ? colOf(n.textColor, T.textPrimary) : (b.enabled ? T.textPrimary : T.textDim),
        n.align, n.center, n.font || 'bold', n.wrapW || 0, b.px, n.lineH || 0, !!n.ellipsis);
    }
    this._selRun = savedRun;
  },
  Button(n, b) {
    const T = this.T, x = b.x, y = b.y, w = b.w, h = b.h, enabled = b.enabled;
    const v = n.variant || '';
    const std = v === 'atelier-primary' || v === 'atelier-ghost';
    const ghost = v === 'atelier-ghost' || (!std && v === 'ghost');
    const rad = std ? RAD.atelier : RAD.button;
    const lbl = n.icon ? (n.icon + ' ' + (n.text || '')) : (n.text || '');
    if (!ghost) {
      this.fill(x, y, w, h, n.color ? colOf(n.color) : (enabled ? T.accent : T.accentDim), rad);
      this.label(lbl, x, y, w, h,
        n.textColor ? colOf(n.textColor, T.textOnAccent) : (enabled ? T.textOnAccent : T.textDim),
        'center', true, n.font || 'bold', 0, b.px, 0, !!n.ellipsis);
    } else {
      this.fill(x, y, w, h, n.color ? colOf(n.color) : T.panel, rad);
      this.outline(x, y, w, h, n.highlight ? T.accent : T.panelEdge, rad);
      // 字の寄せ（KaedeUiPainter の従来パスの ghost と同じ・2026-10-02）。既定は中央。
      // align=left/right は左右に 10px の内側余白を取って寄せる（事件の図の札）。Component Standard の ghost は中央のまま
      const side = !std && (n.align === 'left' || n.align === 'right');
      this.label(lbl, side ? x + 10 : x, y, side ? w - 20 : w, h,
        n.textColor ? colOf(n.textColor, T.textPrimary) : (enabled ? T.textPrimary : T.textDim),
        side ? n.align : 'center', true, n.font || 'bold', 0, b.px, 0, !!n.ellipsis);
    }
    this.addHit(n, x, y, w, h);
  },
  TextInput(n, b) {
    const T = this.T, x = b.x, y = b.y, w = b.w, h = b.h, px = b.px;
    this.fill(x, y, w, h, n.color ? colOf(n.color) : T.track, RAD.panel);
    this.outline(x, y, w, h, n.focused ? T.accent : T.panelEdge, RAD.panel);
    if (!this.domTextInputs) {
      this.label(n.text || n.placeholder || '', x + 12, y, w - 24, h,
        n.text ? T.textPrimary : T.textDim, 'left', false, 'main', 0, px, 0, true);
    }
    const sid = this._scrollId();
    const sdy = sid ? (this.scroll.get(sid) || 0) : 0;
    // 編集器の置き場（画面座標）と、配置・当たりと同じ**操作スコープ**（焦点の群）と**クリップ**（ScrollView の見える範囲との交わり。
    // 無ければ欄そのもの。見える部分が無ければ null）。attach はこれで <input> を置き・切り・無効にする（F1/F2・2026-10-04）。
    // スクロールで見えない欄（_hitOnly）も clip: null で出す＝「画面に出ている欄」の集合が UE（描画命令の TextField）と同じになる
    const sy = y - sdy;
    const cr = this._clipRect();
    const cx0 = Math.max(x, cr.x), cy0 = Math.max(sy, cr.y), cx1 = Math.min(x + w, cr.x + cr.w), cy1 = Math.min(sy + h, cr.y + cr.h);
    const clip = (cx1 > cx0 && cy1 > cy0) ? { x: cx0, y: cy0, w: cx1 - cx0, h: cy1 - cy0 } : null;
    this.textInputs.push({ id: n.id || '', bindVar: n.bindVar || '', action: n.action || '',
      placeholder: n.placeholder || '', fontSize: px || this.defPx, enabled: b.enabled, maxLen: n.maxLen || 0,
      x: x, y: sy, w: w, h: h, group: this._focusGroup, scrollId: sid, clip: clip });
    this.addHit(n, x, y, w, h);
  },
  ScrollView(n, b) {
    const x = b.x, y = b.y, w = b.w, h = b.h, pad = b.pad, gap = b.gap, kids = b.kids;
    if (paintsContainer(n)) this._paintContainer(n, x, y, w, h, RAD.panel);
    let contentH = pad;
    for (const k of kids) contentH += (k.margin || 0) + measureStackH(k) + (k.margin || 0) + gap;
    if (kids.length) contentH -= gap;
    contentH += pad;
    const barW = contentH > h ? SCROLLBAR_W : 0;
    const sid = n.id || ('sv@' + x + ',' + y);
    const maxOff = Math.max(0, contentH - h);
    let off = Math.min(this.scroll.get(sid) || 0, maxOff);
    this.scroll.set(sid, off);

    if (!this._hitOnly) this._op({ op: 'clip', x: x, y: y, w: w, h: h, ty: off });   // 箱で切り、中身をスクロール量だけ上へ
    this._clip.push(sid);
    // 見える範囲（画面座標・外の ScrollView と交わる）。文字欄の編集器のクリップに写す（F2）
    const pc = this._clipRect();
    const rx0 = Math.max(x, pc.x), ry0 = Math.max(y, pc.y), rx1 = Math.min(x + w, pc.x + pc.w), ry1 = Math.min(y + h, pc.y + pc.h);
    this._clipRects.push({ x: rx0, y: ry0, w: Math.max(0, rx1 - rx0), h: Math.max(0, ry1 - ry0) });
    const prevBand = this._band;
    this._band = { top: y + off, bottom: y + off + h };
    let cy = y + pad;
    for (const kk of kids) {
      const k = Object.assign({}, kk);
      cy += (k.margin || 0);
      k.x = x + pad + (k.margin || 0);
      if (!k.w) k.w = w - 2 * pad - 2 * (k.margin || 0) - barW;
      const ch = measureStackH(k);
      k.h = ch; k.y = cy;
      this.node(k);   // 画面外の子は nodeBody が「配置と当たりだけ」にする
      cy += ch + (k.margin || 0) + gap;
    }
    this._band = prevBand;
    this._clip.pop();
    this._clipRects.pop();
    if (!this._hitOnly) this._op({ op: 'unclip' });

    if (maxOff > 0) {   // SKaedeUiCanvas のスクロール条（描画命令ではなくキャンバスが描くものなので記録しない）
      this._noRecord++;
      const tx = x + w - BAR_W - 2;
      this.fill(tx, y, BAR_W, h, 'rgba(255,255,255,0.06)', 0);
      const th = Math.max(24, h * (h / contentH));
      const ty = y + (off / maxOff) * (h - th);
      this.fill(tx, ty, BAR_W, th, 'rgba(224,160,74,0.8)', 0);
      this._noRecord--;
    }
    this.scrollAreas.push({ x: x, y: y, w: w, h: h, contentH: contentH, id: sid });
  },
  Modal(n, b) {
    const T = this.T, pad = b.pad, gap = b.gap;
    if (n.dim !== false) this.fill(0, 0, this.vpW, this.vpH, T.dim, 0);
    const savedFocusGroup = this._focusGroup;
    // Modal は暗幕ごと別の群（方向移動で背面へ出ない・操作対象が無くても背面へ漏れない）。群の番号は**描いた順の通し番号**＝
    // 同じ親に 2 つ並んでも、後に描いた（画面で手前の）Modal が最前面になる（F4・仕様 §5.4・2026-10-04。入れ子の深さでは兄弟が同じ群になった）
    this._focusGroup = ++this._modalSeq;
    const absorb = { x: 0, y: 0, w: this.vpW, h: this.vpH, action: n.action || '', id: n.id || '', type: 'Modal',
                     text: '', scrollId: '', focusable: false, group: this._focusGroup };   // 背面吸収
    absorb.key = hitKey(absorb);
    this.hits.push(absorb);
    const pw = b.w > 0 ? b.w : 480, ph = b.h > 0 ? b.h : 320;
    const pxx = b.x > 0 ? b.x : (this.vpW - pw) / 2;
    const pyy = b.y > 0 ? b.y : (this.vpH - ph) / 2;
    this.fill(pxx, pyy, pw, ph, n.color ? colOf(n.color) : T.panel, RAD.modal);
    this.outline(pxx, pyy, pw, ph, T.panelEdge, RAD.modal);
    if (n.text) this.label(n.text, pxx, pyy + 4, pw, 36, T.textPrimary, 'center', true, 'bold', 0, b.px, 0, false);
    let cy = pyy + (n.text ? 48 : pad);
    for (const kk of b.kids) {
      const k = Object.assign({}, kk);
      cy += (k.margin || 0);
      k.x = pxx + pad + (k.margin || 0);
      if (!k.w) k.w = pw - 2 * pad - 2 * (k.margin || 0);
      k.y = cy;
      this.node(k);
      cy += measureStackH(k) + (k.margin || 0) + gap;
    }
    this._focusGroup = savedFocusGroup;
  },
  VerticalLayout(n, b) {
    const x = b.x, y = b.y, w = b.w, h = b.h, pad = b.pad, gap = b.gap, kids = b.kids;
    if (paintsContainer(n)) this._paintContainer(n, x, y, w, h, RAD.panel);
    this.addHit(n, x, y, w, h);
    let sumFlex = 0, fixed = 0;
    for (const k of kids) {
      sumFlex += (k.flex || 0);
      fixed += 2 * (k.margin || 0) + ((k.flex > 0) ? 0 : measureStackH(k)) + gap;
    }
    const leftover = Math.max(0, (h - 2 * pad) - fixed);
    let cy = y + pad;
    for (const kk of kids) {
      const k = Object.assign({}, kk);
      cy += (k.margin || 0);
      k.x = x + pad + (k.margin || 0);
      if (!k.w) k.w = w - 2 * pad - 2 * (k.margin || 0);
      let ch;
      if (k.flex > 0 && sumFlex > 0) { ch = Math.max(k.minH || 0, Math.round(leftover * (k.flex / sumFlex))); k.h = ch; }
      else {
        ch = measureStackH(k);
        // 高さの無い入れ物の子は、測った高さを持たせる（仕様 §5.1。無いと地と当たりが高さ 0 で描かれた。2026-10-12）
        if (!k.h && (k.type === 'VerticalLayout' || k.type === 'HorizontalLayout' || k.type === 'GridLayout')) k.h = ch;
      }
      k.y = cy;
      this.node(k);
      cy += ch + (k.margin || 0) + gap;
    }
  },
  HorizontalLayout(n, b) {
    const x = b.x, y = b.y, w = b.w, h = b.h, pad = b.pad, gap = b.gap, kids = b.kids;
    if (paintsContainer(n)) this._paintContainer(n, x, y, w, h, RAD.panel);
    this.addHit(n, x, y, w, h);
    let sumFlex = 0, fixed = 0;
    for (const k of kids) {
      sumFlex += (k.flex || 0);
      fixed += 2 * (k.margin || 0) + ((k.flex > 0) ? 0 : (k.w > 0 ? k.w : 100)) + gap;
    }
    const leftover = Math.max(0, (w - 2 * pad) - fixed);
    let cx = x + pad;
    for (const kk of kids) {
      const k = Object.assign({}, kk);
      cx += (k.margin || 0);
      k.y = y + pad + (k.margin || 0);
      if (!k.h) k.h = h - 2 * pad - 2 * (k.margin || 0);
      let cw;
      if (k.flex > 0 && sumFlex > 0) { cw = Math.max(k.minW || 0, Math.round(leftover * (k.flex / sumFlex))); k.w = cw; }
      else cw = k.w > 0 ? k.w : 100;
      k.x = cx;
      this.node(k);
      cx += cw + (k.margin || 0) + gap;
    }
  },
  Polyline(n) {
    this.polyline(n.points, colOf(n.color, this.T.panelEdge), n.thickness, n.closed);
  },
  // ---- 設定画面の部品（KaedeUiPainter の Slider / Toggle / ToggleGroup の移植・2026-09-13）----
  // ブラウザ版の外殻（title/menu/config…）を同じ JSON から描くために足した。
  Slider(n, b) {
    // track 全幅 + value ぶんの accent 塗り。h 未指定は 14。当たりは track 全体（押した位置の比が値になる）
    const T = this.T, x = b.x, y = b.y, w = b.w;
    const sh = b.h > 0 ? b.h : 14;
    this.fill(x, y, w, sh, T.track, sh * 0.5);
    const v = Math.max(0, Math.min(1, n.value || 0));
    const fw = Math.floor(w * v);
    if (fw > 0) this.fill(x, y, fw, sh, n.color ? colOf(n.color) : T.accent, sh * 0.5);
    this.addHit(n, x, y, w, sh);
  },
  Toggle(n, b) {
    // チェックボックス（枠 + on 時は内側 accent）。ラベルは右
    const T = this.T, x = b.x, y = b.y, px = b.px;
    const box = b.h > 0 ? b.h : 28;
    this.fill(x, y, box, box, T.panel, 6);
    this.outline(x, y, box, box, n.on ? T.accent : T.panelEdge, 6);
    if (n.on) this.fill(x + 5, y + 5, box - 10, box - 10, T.accent, 4);
    if (n.text) this.label(n.text, x + box + 10, y, 300, box, T.textPrimary, 'left', false, n.font || 'main', 0, px, 0, false);
    this.addHit(n, x, y, box + 10 + (n.text ? this.measure(n.text, false, px) : 0), box);
  },
  ToggleGroup(n, b) {
    // 子を横等分・selected に上部 accent バー
    const T = this.T, kids = b.kids;
    const cnt = kids.length;
    if (!cnt) return;
    const cellW = Math.floor(b.w / cnt);
    for (let i = 0; i < cnt; i++) {
      const k = kids[i];
      const cx = b.x + i * cellW;
      const on = (i === (n.selected || 0));
      if (on) this.fill(cx, b.y, cellW, 3, T.accent, 0);
      this.label(k.text || '', cx, b.y, cellW, b.h, on ? T.textPrimary : T.textDim, 'center', true, k.font || 'main', 0, k.fontSize || 0, 0, false);
      const hit = this.addHit(k, cx, b.y, cellW, b.h);
      if (hit) { hit.type = 'ToggleGroup'; hit.index = i; }   // 事象の value はこの番号（押した項目）
    }
  },
  GridLayout(n, b) {
    // cols 列のグリッド（セル幅は等分・行は各行の最大高で送る。子の h 無しは 28）＝KaedeUiPainter と同じ
    const x = b.x, y = b.y, w = b.w, h = b.h, pad = b.pad, gap = b.gap;
    if (paintsContainer(n)) this._paintContainer(n, x, y, w, h, RAD.panel);
    this.addHit(n, x, y, w, h);
    const cols = Math.max(1, n.cols || 1);
    const cellW = Math.floor((w - 2 * pad - (cols - 1) * gap) / cols);
    let gy = y + pad, colI = 0, rowMaxH = 0;
    for (const kk of b.kids) {
      const k = Object.assign({}, kk);
      const m = k.margin || 0;
      k.x = x + pad + colI * (cellW + gap) + m;
      if (!k.w) k.w = cellW - 2 * m;
      k.y = gy + m;
      k.h = k.h > 0 ? k.h : 28;
      this.node(k);
      rowMaxH = Math.max(rowMaxH, k.h + 2 * m);
      if (++colI >= cols) { colI = 0; gy += rowMaxH + gap; rowMaxH = 0; }
    }
  },
  Ellipse(n, b) {
    // x/y/w/h が外接矩形。fill=false なら輪郭だけ（thickness）。色の既定は textPrimary
    if (b.w > 0 && b.h > 0 && !this._hitOnly) {
      this._op({ op: 'ellipse', x: b.x, y: b.y, w: b.w, h: b.h, css: colOf(n.color, this.T.textPrimary), fill: n.fill !== false, thickness: Math.max(n.thickness || 1, 0.1) });
    }
    for (const k of b.kids) this.node(k);
  },
  Image(n, b) {
    const x = b.x, y = b.y, w = b.w, h = b.h;
    // 下地: 色があれば色、画像も色も無ければ panel（KaedeUiPainter と同じ。画像があるときは塗らない）
    if (n.color) this.fill(x, y, w, h, colOf(n.color), 0);
    else if (!n.sprite) this.fill(x, y, w, h, this.T.panel, 0);
    // 画像の解決（sprite → URL）と読み込みは描くとき（paint）。配置は鍵だけ持つ
    if (n.sprite && !this._hitOnly) this._op({ op: 'image', sprite: n.sprite, x: x, y: y, w: w, h: h, fit: n.fit || 'contain' });
    this.addHit(n, x, y, w, h);
    for (const k of b.kids) this.node(k);
  },
};

// 部品の高さを描かずに測る（仕様 §5。値を差し込み済みの部品を渡す。KaedeUI::MeasureNodeHeight と同じ式）
Renderer.prototype.measureNode = function (n) { return measureStackH(n, this); };

// 論理座標→キャンバス画素の拡大率（ブラウザ版の外殻が窓の大きさに合わせるため・2026-09-13）。
// 1 のままなら従来どおり。hitAt / wheelAt は論理座標で受けるので、呼ぶ側が割ってから渡す。
Renderer.prototype.setScale = function (s) { this.scale = s > 0 ? s : 1; };

// ---- 配置（layout）: 描かずに、描画命令（ops）・当たり（hits）・文字欄（textInputs）・スクロール領域（scrollAreas）を作り、焦点を整える ----
// bg: 画面全体の下地。省略でテーマの bg（従来）。null なら塗らない（下の絵を透かす）。
// rgba 文字列なら半透明の暗幕（UE の SKaedeAdvShell はタイトル/エンディングで 0.55 の暗幕、メニュー系は透明）。
// 戻り値は配置の結果 { ops, hits, textInputs, scrollAreas, prims }（同じものが this にも残る）。描くのは paint()
Renderer.prototype.layout = function (root, bg) {
  // 画面が変わったら画面全体を 160ms で浮かべ、前の画面の動きは捨て、**その画面の状態**（スクロール量・焦点・控えた鍵）へ切り替える（F3）
  //（状態の切り替えそのものは screenId を入れた瞬間に済んでいる＝host が描く前に R.scroll を触っても、その画面の状態に入る）
  if (this.screenId !== this._lastScreen) { this._lastScreen = this.screenId; this._screenT0 = performance.now(); this.motion.clear(); }
  const prevKey = this.focusKey, prevTop = this._lastTop;
  this.hits = []; this.scrollAreas = []; this._clip = []; this._clipRects = []; this.textInputs = []; this.pulsing = false; this._ops = [];
  this._focusGroup = 0; this._modalSeq = 0; this._hitOnly = 0; this._noRecord = 0; this._motionKey = ''; this._groupKey = ''; this._groupCount = new Map(); this._seen = new Set();
  this.selLines = []; this.selAreas = []; this._selGroup = ''; this._selRunSeq = 0; this._selOrd = 0; this._selRun = -1;
  const screenRaw = (performance.now() - this._screenT0) / 160;
  const fillBg = bg === undefined ? this.T.bg : bg;
  if (fillBg) this.fill(0, 0, this.vpW, this.vpH, fillBg, 0);
  // anchor の解決（KaedeUiPainter::ApplyAnchors の移植・2026-09-13）。木は書き換えずコピーに解決する
  const resolved = applyAnchors(cloneTree(root), 0, 0, this.vpW, this.vpH, this.vpW, this.vpH);
  const prevStack = stackRenderer;
  stackRenderer = this;
  const fading = screenRaw < 1;
  if (fading) { this.pulsing = true; this._op({ op: 'push', alpha: ease.easeOutCubic(clamp01(screenRaw)), tx: 0, ty: 0, scale: 1, cx: 0, cy: 0 }); }
  try { this.node(resolved); } finally { stackRenderer = prevStack; }
  if (fading) this._op({ op: 'pop' });
  for (const k of [...this.motion.keys()]) if (!this._seen.has(k)) this.motion.delete(k);   // 描かれなかった部品の動きは捨てる
  // 焦点を整える（Modal の開閉・部品の消失・並べ替え）。鍵で追うので同じ action の別の部品へは移らない
  this.reconcileFocus(prevKey, prevTop);
  // 文字の選択: 器ごとの行の番号を引けるようにし、選んでいた行の文字が変わった・無くなったら選択を消す（§4.4）
  this._selGroupLines = new Map();
  this.selLines.forEach((l, i) => { if (!this._selGroupLines.has(l.group)) this._selGroupLines.set(l.group, []); this._selGroupLines.get(l.group).push(i); });
  this._verifySelection();
  // 焦点の枠（方向キーで操作している間だけ）: 部品の 3px 外側に太さ 3 の accent の枠（SKaedeUiCanvas と同じ橙 #e0a04a）。キャンバスの飾り
  const f = this.focusVisible ? this.focusedHit() : null;
  if (f) {
    const dy = f.scrollId ? (this.scroll.get(f.scrollId) || 0) : 0;
    const X = f.x - 3, Y = f.y - 3 - dy, W = f.w + 6, Hh = f.h + 6, th = 3, css = 'rgba(224,160,74,1)';
    this._noRecord++;
    this.fill(X, Y, W, th, css, 0); this.fill(X, Y + Hh, W, th, css, 0); this.fill(X, Y, th, Hh, css, 0); this.fill(X + W, Y, th, Hh + th, css, 0);
    this._noRecord--;
  }
  this.ops = this._ops;
  this.prims = this.recording ? primsOf(this.ops) : [];
  return { ops: this.ops, hits: this.hits, textInputs: this.textInputs, scrollAreas: this.scrollAreas, prims: this.prims };
};
// 照合に記録する描画命令（UE の FKaedeDrawCmd と同じ範囲: 矩形・枠・文字の行・画像・線・楕円。飾りは除く）
function primsOf(ops) {
  const out = [];
  for (const o of ops) {
    if (o.chrome) continue;
    switch (o.op) {
      case 'rect': out.push({ kind: 'Rect', x: o.x, y: o.y, w: o.w, h: o.h }); break;
      case 'outline': out.push({ kind: 'RectOutline', x: o.x, y: o.y, w: o.w, h: o.h }); break;
      case 'text': out.push({ kind: 'Text', text: o.text, x: o.px, y: o.py }); break;
      case 'ellipse': out.push({ kind: 'Ellipse', x: o.x, y: o.y, w: o.w, h: o.h }); break;
      case 'image': out.push({ kind: 'Image', x: o.x, y: o.y, w: o.w, h: o.h }); break;
      case 'lines': {
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (const p of o.pts) { x0 = Math.min(x0, p[0]); y0 = Math.min(y0, p[1]); x1 = Math.max(x1, p[0]); y1 = Math.max(y1, p[1]); }
        out.push({ kind: 'Lines', x: x0, y: y0, w: x1 - x0, h: y1 - y0, n: o.pts.length + (o.closed ? 1 : 0) });
        break;
      }
      default: break;
    }
  }
  return out;
}
// ---- 描画（paint）: 配置の結果（ops）を Canvas2D へ。画像はここで解決して読む（読み終えたら onImageLoad で host が描き直す）----
Renderer.prototype.paint = function () {
  const c = this.c, s = this.scale || 1;
  c.setTransform(1, 0, 0, 1, 0, 0);
  c.clearRect(0, 0, c.canvas.width, c.canvas.height);
  c.setTransform(s, 0, 0, s, 0, 0);
  c.textAlign = 'left';
  for (const o of (this.ops || [])) {
    switch (o.op) {
      case 'rect': c.fillStyle = o.css; roundRect(c, o.x, o.y, o.w, o.h, o.rad); c.fill(); break;
      case 'outline': c.strokeStyle = o.css; c.lineWidth = 1; roundRect(c, o.x + 0.5, o.y + 0.5, o.w - 1, o.h - 1, o.rad); c.stroke(); break;
      case 'text': c.font = o.font; c.fillStyle = o.css; c.textBaseline = o.baseline; c.fillText(o.text, o.x, o.y); break;
      case 'lines': {
        c.strokeStyle = o.css; c.lineWidth = o.thickness; c.lineJoin = 'round'; c.lineCap = 'round';
        c.beginPath(); c.moveTo(o.pts[0][0], o.pts[0][1]);
        for (let i = 1; i < o.pts.length; i++) c.lineTo(o.pts[i][0], o.pts[i][1]);
        if (o.closed) c.closePath();
        c.stroke();
        break;
      }
      case 'ellipse': {
        c.beginPath(); c.ellipse(o.x + o.w / 2, o.y + o.h / 2, o.w / 2, o.h / 2, 0, 0, Math.PI * 2);
        if (o.fill) { c.fillStyle = o.css; c.fill(); } else { c.strokeStyle = o.css; c.lineWidth = o.thickness; c.stroke(); }
        break;
      }
      case 'image': {
        const img = this.imageFor(o.sprite);
        if (!img) break;
        // fit: contain（既定・枠内に縦横比のまま収める）/ cover（枠を埋めてはみ出しを切る）/ stretch（引き伸ばす）
        let dx = o.x, dy = o.y, dw = o.w, dh = o.h;
        if (o.fit === 'contain' || o.fit === 'cover') {
          const sc = (o.fit === 'cover' ? Math.max : Math.min)(o.w / img.naturalWidth, o.h / img.naturalHeight);
          dw = img.naturalWidth * sc; dh = img.naturalHeight * sc;
          dx = o.x + (o.w - dw) / 2; dy = o.y + (o.h - dh) / 2;
        }
        if (o.fit === 'cover') { c.save(); roundRect(c, o.x, o.y, o.w, o.h, 0); c.clip(); }
        c.drawImage(img, dx, dy, dw, dh);
        if (o.fit === 'cover') c.restore();
        break;
      }
      case 'push': {
        c.save();
        c.globalAlpha *= o.alpha;
        c.translate(o.tx, o.ty);
        if (o.scale !== 1) { c.translate(o.cx, o.cy); c.scale(o.scale, o.scale); c.translate(-o.cx, -o.cy); }
        break;
      }
      case 'pop': c.restore(); break;
      case 'clip': c.save(); roundRect(c, o.x, o.y, o.w, o.h, 0); c.clip(); c.translate(0, -o.ty); break;
      case 'unclip': c.restore(); break;
      default: break;
    }
  }
  this._paintSelection(c);
};
// 文字の選択の反転と「コピーしました」の札（キャンバスの飾り。配置にも照合の prims にも入らない・設計 10/07/01 §4.6）。
// 反転は文字の上に半透明で重ねる（T.accent の 35%）。ScrollView の中の行はその見える範囲で切る
Renderer.prototype._paintSelection = function (c) {
  const S = this._sel;
  if (S) {
    const a = selOrder(S.anchor, S.focus), A = a[0], B = a[1];
    const m = (this.T.accent.match(/[\d.]+/g) || [224, 160, 74]).map(Number);
    c.fillStyle = 'rgba(' + m[0] + ',' + m[1] + ',' + m[2] + ',0.35)';
    for (let li = A.line; li <= B.line; li++) {
      const ln = this.selLineOf(S.group, li);
      if (!ln) continue;
      const area = ln.scrollId ? this.scrollAreas.find((x) => x.id === ln.scrollId) : null;
      const y = ln.y - (ln.scrollId ? (this.scroll.get(ln.scrollId) || 0) : 0);
      if (area ? (y + ln.lineH <= area.y || y >= area.y + area.h) : (y + ln.lineH <= 0 || y >= this.vpH)) continue;   // 見える範囲の外は測らない
      const n = Array.from(ln.text).length;
      const c0 = li === A.line ? A.ch : 0, c1 = li === B.line ? B.ch : n;
      if (c1 <= c0) continue;
      const x0 = ln.x + this.selPrefixW(ln, c0), x1 = ln.x + this.selPrefixW(ln, c1);
      if (area) { c.save(); c.beginPath(); c.rect(area.x, area.y, area.w, area.h); c.clip(); }
      c.fillRect(x0, y, x1 - x0, ln.lineH);
      if (area) c.restore();
    }
  }
  if (this.copiedAt >= 0 && performance.now() - this.copiedAt < 900 && this.copiedPos) {
    const msg = 'コピーしました', pad = 6;
    c.font = fontCss(false, 13);
    const tw = c.measureText(msg).width, th = Math.round(13 * SLATE_PT_TO_PX) * 1.3;
    const w = tw + 2 * pad, h = th + 2 * pad;
    const x = Math.max(0, Math.min(this.copiedPos.x + 12, this.vpW - w)), y = Math.max(0, Math.min(this.copiedPos.y + 16, this.vpH - h));
    c.fillStyle = 'rgba(21,26,30,0.9)'; roundRect(c, x, y, w, h, 4); c.fill();
    c.fillStyle = '#ffffff'; c.textBaseline = 'middle'; c.fillText(msg, x + pad, y + h / 2);
  }
};
// sprite（画像の鍵）→ 読み終えた画像。無ければ読み始めて null（読み終えたら onImageLoad）。
// 鍵から URL への解決は host が imageSource で差し替えられる（既定は鍵をそのまま URL に。OSS 計画 §4「資源」）。
// 読めなければ onImageError(sprite, url) で知らせ（無ければ console.warn を画像ごとに 1 度）、2 秒後に控えを捨てて描き直しを頼む＝次に描くとき読み直す
//（UE の SKaedeUiCanvas が読めない画像を 2 秒ごとに読み直すのと同じ）
const IMAGE_RETRY_MS = 2000;
Renderer.prototype.imageFor = function (sprite) {
  let img = this.images.get(sprite);
  if (!img && typeof Image !== 'undefined') {
    img = new Image();
    const url = this.imageSource ? this.imageSource(sprite) : sprite;
    img.onload = () => { if (this.onImageLoad) this.onImageLoad(sprite); };
    img.onerror = () => {
      if (this.onImageError) this.onImageError(sprite, url);
      else if (!this._imageWarned.has(sprite)) { this._imageWarned.add(sprite); console.warn('MomijiUI: 画像が読めない（2 秒ごとに読み直す）: ' + sprite + ' → ' + url); }
      img._retry = setTimeout(() => {
        if (this.images.get(sprite) !== img) return;   // 捨てた後（dispose）
        this.images.delete(sprite);
        if (this.onImageLoad) this.onImageLoad(sprite);   // 描き直してもらう＝読み直す
      }, IMAGE_RETRY_MS);
    };
    img.src = url;
    this.images.set(sprite, img);
  }
  return img && img.complete && img.naturalWidth > 0 ? img : null;
};
// 配置して描く（従来の入口）。attach したら、描く前と後に attach の受け口（_host）を呼ぶ（画面の切り替え・文字欄を揃える・動きの描き直し）
Renderer.prototype.draw = function (root, bg) {
  const host = this._host;
  if (host) host.beforeDraw();
  this.layout(root, bg);
  this.paint();
  if (host) host.afterDraw();
};
// 持っている画像・計測の控えを捨てる（画面／host を壊すとき。OSS 計画 §4「終了」）
Renderer.prototype.dispose = function () {
  for (const img of this.images.values()) { try { clearTimeout(img._retry); img.onload = null; img.onerror = null; img.src = ''; } catch (e) { /* 読み中でも構わない */ } }
  this.images.clear(); this._mw.clear(); this.motion.clear(); this._screens.clear();
  this.ops = []; this.hits = []; this.textInputs = []; this.scrollAreas = []; this.prims = [];
};

// ---- 文字の選択とコピー（2026-10-07・設計 Docs/Reports/2026/10/07/01・仕様 momiji-ui-framework.md §7「選択」）----
// UE の SKaedeUiCanvas（SelectableGroupAt / SelPosAt / SelTextOf / VerifySelection）と同じ規則。位置は {line, ch}:
// line は器の中の選べる行の描画順の番号、ch はコードポイントで 0〜文字数
const selOrder = (a, b) => (b.line < a.line || (b.line === a.line && b.ch < a.ch) ? [b, a] : [a, b]);
Renderer.prototype.selLineOf = function (group, line) {
  const ls = this._selGroupLines.get(group);
  return ls && line >= 0 && line < ls.length ? this.selLines[ls[line]] : null;
};
// 先頭から k 字の送り幅（描いたときと同じ書体・大きさ＝描く位置と当たりが同じ関数から出る）
Renderer.prototype.selPrefixW = function (ln, k) {
  if (k <= 0) return 0;
  return this.measure(Array.from(ln.text).slice(0, k).join(''), ln.bold, ln.px);
};
// 行の X からのずれ → 文字番号: dx を含む文字 k の中点より左なら k、右なら k+1（§4.2）
Renderer.prototype.selChAt = function (ln, dx) {
  const n = Array.from(ln.text).length;
  if (dx <= 0 || n === 0) return 0;
  if (dx >= this.selPrefixW(ln, n)) return n;
  let lo = 0, hi = n - 1;
  while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (this.selPrefixW(ln, mid) <= dx) lo = mid; else hi = mid - 1; }
  const w0 = this.selPrefixW(ln, lo), w1 = this.selPrefixW(ln, lo + 1);
  return dx < (w0 + w1) / 2 ? lo : lo + 1;
};
// 論理座標の点を含む、いま選べる器の鍵（最前面の群・見える範囲の中・押せる当たりの上ではない）。無ければ ''
Renderer.prototype.selAreaAt = function (x, y) {
  const hit = this.hitAt(x, y);
  if (hit && hit.action) return '';
  const top = this.topFocusGroup();
  for (let i = this.selAreas.length - 1; i >= 0; i--) {   // 後に出た器＝内側の器が勝つ
    const a = this.selAreas[i];
    if (a.focusGroup !== top || !this._selGroupLines.has(a.group)) continue;
    let ay = a.y;
    if (a.scrollId) {
      const v = this.scrollAreas.find((s) => s.id === a.scrollId);
      if (v && (y < v.y || y >= v.y + v.h || x < v.x || x >= v.x + v.w)) continue;   // 見える範囲の外
      ay -= this.scroll.get(a.scrollId) || 0;
    }
    if (x >= a.x && x < a.x + a.w && y >= ay && y < ay + a.h) return a.group;
  }
  return '';
};
// 点 → {line, ch}。行は縦の当たり [y, y+lineH) に入る行、無ければ縦にいちばん近い行。同じ距離なら横に近い行、それも同じなら先の行（§4.2）
Renderer.prototype.selPosAt = function (group, x, y) {
  const ls = this._selGroupLines.get(group);
  if (!ls || !ls.length) return { line: 0, ch: 0 };
  const vdist = (ln) => {
    const ly = y + (ln.scrollId ? (this.scroll.get(ln.scrollId) || 0) : 0);
    return ly < ln.y ? ln.y - ly : (ly >= ln.y + ln.lineH ? ly - (ln.y + ln.lineH) : 0);
  };
  let bestV = Infinity;
  for (const k of ls) bestV = Math.min(bestV, vdist(this.selLines[k]));
  let best = -1, bestH = Infinity;
  ls.forEach((k, i) => {
    const ln = this.selLines[k];
    if (vdist(ln) > bestV) return;
    const adv = this.selPrefixW(ln, Array.from(ln.text).length);
    const hd = x < ln.x ? ln.x - x : (x > ln.x + adv ? x - (ln.x + adv) : 0);
    if (hd < bestH) { bestH = hd; best = i; }
  });
  const line = Math.max(0, best), ln = this.selLines[ls[line]];
  return { line: line, ch: this.selChAt(ln, x - ln.x) };
};
// 写す文: 行ごとに文字番号の範囲。同じ Label の折り返しの続きは何も挟まず、Label が変わるところは改行。先頭・末尾の改行は付けない（§4.5）
Renderer.prototype.selTextOf = function (group, a, b) {
  const o = selOrder(a, b), A = o[0], B = o[1];
  let out = '', prevRun = null;
  for (let li = A.line; li <= B.line; li++) {
    const ln = this.selLineOf(group, li);
    if (!ln) break;
    const cps = Array.from(ln.text), n = cps.length;
    const c0 = Math.max(0, Math.min(li === A.line ? A.ch : 0, n)), c1 = Math.max(0, Math.min(li === B.line ? B.ch : n, n));
    if (li > A.line && ln.run !== prevRun) out += '\n';
    if (c1 > c0) out += cps.slice(c0, c1).join('');
    prevRun = ln.run;
  }
  return out.replace(/^\n+|\n+$/g, '');
};
// 選び始める（押した点が anchor）・伸ばす（いまの点が focus）。範囲の行の文字を控える（描き直しで変わったら消す）
Renderer.prototype.selBegin = function (group, x, y) {
  const p = this.selPosAt(group, x, y);
  this._sel = { group: group, anchor: p, focus: p, firstLine: p.line, texts: [] };
  this.selExtend(x, y);
};
Renderer.prototype.selExtend = function (x, y) {
  const S = this._sel;
  if (!S) return false;
  const f = this.selPosAt(S.group, x, y);
  const changed = f.line !== S.focus.line || f.ch !== S.focus.ch || !S.texts.length;
  S.focus = f;
  S.firstLine = Math.min(S.anchor.line, f.line);
  const last = Math.max(S.anchor.line, f.line);
  S.texts = [];
  for (let li = S.firstLine; li <= last; li++) { const ln = this.selLineOf(S.group, li); S.texts.push(ln ? ln.text : ''); }
  return changed;
};
Renderer.prototype._verifySelection = function () {
  const S = this._sel;
  if (!S) return;
  for (let i = 0; i < S.texts.length; i++) {
    const ln = this.selLineOf(S.group, S.firstLine + i);
    if (!ln || ln.text !== S.texts[i]) { this._sel = null; return; }
  }
};
Renderer.prototype.clearSelection = function () { const had = !!this._sel; this._sel = null; return had; };
// いまの選択 { group, from: [line, ch], to: [line, ch], text }（無ければ null）。照合と host が読む
Object.defineProperty(Renderer.prototype, 'selection', {
  get() {
    const S = this._sel;
    if (!S) return null;
    const o = selOrder(S.anchor, S.focus);
    return { group: S.group, from: [o[0].line, o[0].ch], to: [o[1].line, o[1].ch], text: this.selTextOf(S.group, o[0], o[1]) };
  },
});
// 選べる行の上か（I ビームを出す）
Renderer.prototype.selTextAt = function (x, y) {
  const g = this.selAreaAt(x, y);
  if (!g) return false;
  return (this._selGroupLines.get(g) || []).some((k) => {
    const ln = this.selLines[k];
    const ly = y + (ln.scrollId ? (this.scroll.get(ln.scrollId) || 0) : 0);
    return ly >= ln.y && ly < ln.y + ln.lineH && x >= ln.x && x <= ln.x + this.selPrefixW(ln, Array.from(ln.text).length);
  });
};
// 検証の口: p0 で押して p1 で離したときの選択（attach の押す→動かす→離すと同じ規則。4px 未満・器の外・当たりの上なら選択なし）。クリップボードには入れない
Renderer.prototype.selectAt = function (p0, p1) {
  const g = this.selAreaAt(p0[0], p0[1]);
  if (!g || Math.hypot(p1[0] - p0[0], p1[1] - p0[1]) < 4) { this._sel = null; return null; }
  this.selBegin(g, p0[0], p0[1]);
  this.selExtend(p1[0], p1[1]);
  const s = this.selection;
  if (s && s.from[0] === s.to[0] && s.from[1] === s.to[1]) { this._sel = null; return null; }
  return s;
};

// anchor: fill / safe / top / bottom / left / right / center を、安全域 (rx,ry,rw,rh) と画面 (vpW,vpH) で座標へ
function applyAnchors(n, rx, ry, rw, rh, vpW, vpH) {
  if (!n) return n;
  const a = n.anchor;
  if (a) {
    const w = n.w || 0, h = n.h || 0;
    if (a === 'fill') { n.x = 0; n.y = 0; n.w = vpW; n.h = vpH; }
    else if (a === 'safe') { n.x = rx; n.y = ry; n.w = rw; n.h = rh; }
    else if (a === 'top') { n.x = rx; n.y = ry; n.w = rw; }
    else if (a === 'bottom') { n.x = rx; n.y = ry + rh - h; n.w = rw; }
    else if (a === 'left') { n.x = rx; n.y = ry; n.h = rh; }
    else if (a === 'right') { n.x = rx + rw - w; n.y = ry; n.h = rh; }
    else if (a === 'center') { n.x = rx + Math.floor((rw - w) / 2); n.y = ry + Math.floor((rh - h) / 2); }
  }
  for (const c of (n.children || [])) applyAnchors(c, rx, ry, rw, rh, vpW, vpH);
  return n;
}

// 論理座標での当たり判定。**後に積んだものが勝つ**（UE の host と同じ後勝ち）。
Renderer.prototype.hitAt = function (x, y) {
  for (let i = this.hits.length - 1; i >= 0; i--) {
    const it = this.hits[i];
    let dy = 0;
    if (it.scrollId) {
      dy = this.scroll.get(it.scrollId) || 0;
      const a = this.scrollAreas.find((s) => s.id === it.scrollId);
      if (a && (y < a.y || y >= a.y + a.h)) continue;   // 見える範囲（クリップ）の外は押せない
    }
    if (x >= it.x && x < it.x + it.w && y >= it.y - dy && y < it.y + it.h - dy) return it;
  }
  return null;
};

Renderer.prototype.wheelAt = function (x, y, dy) {
  for (let i = this.scrollAreas.length - 1; i >= 0; i--) {
    const a = this.scrollAreas[i];
    if (x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h) {
      const maxOff = Math.max(0, a.contentH - a.h);
      const off = Math.max(0, Math.min((this.scroll.get(a.id) || 0) + dy, maxOff));
      this.scroll.set(a.id, off);
      return true;
    }
  }
  return false;
};

// ---- 木の前処理（UE の host がキャンバスへ渡す前にやっていること・2026-09-13）----
// SKaedeAdvShell::RefreshCanvas の順に: 変数を木へ書く → Label 差し込み → enabled →
// enabledIf/highlightIf → showIf/hideIf で刈る。ブラウザ版の外殻（app.js）がこの順で呼ぶ。
//
// 条件式は C++ の kaedevn::ui::evalCondExpr と同じ意味（Scripts/lib/adv-uitree.mjs の移植と同文）:
//   or := and ("||" and)* / and := unary ("&&" unary)* / unary := "!" unary | primary
//   primary := "(" or ")" | <項>   項は「変数 OP 数値」/「数値」/「変数の真偽」
const numOf = (vars, key) => (typeof vars[key] === 'number' ? vars[key] : 0);
function evalCond(expr, vars) {
  // 名前には . を使える（C++ の Interp.cpp と同じ・2026-10-11）
  const m = /^\s*([A-Za-z_][\w.]*)\s*(==|!=|>=|<=|>|<)\s*(-?\d+(?:\.\d+)?)\s*$/.exec(expr);
  if (m) {
    const v = numOf(vars, m[1]), num = parseFloat(m[3]);
    switch (m[2]) {
      case '==': return v === num; case '!=': return v !== num;
      case '>=': return v >= num; case '<=': return v <= num;
      case '>': return v > num; case '<': return v < num;
    }
  }
  const lit = /^\s*(-?\d+(?:\.\d+)?)\s*$/.exec(expr);
  if (lit) return parseFloat(lit[1]) !== 0;
  return numOf(vars, expr.trim()) !== 0;
}
function evalCondExpr(expr, vars) {
  if (!expr) return true;
  if (!/[&|()!]/.test(expr)) return evalCond(expr, vars);
  let i = 0; const s = expr;
  const ws = () => { while (i < s.length && /\s/.test(s[i])) i++; };
  const eat = (t) => { ws(); if (s.startsWith(t, i)) { i += t.length; return true; } return false; };
  function primary() {
    ws();
    if (eat('(')) { const v = or_(); eat(')'); return v; }
    const st = i;
    while (i < s.length) {
      if (s.startsWith('&&', i) || s.startsWith('||', i) || s[i] === ')') break;
      if (s[i] === '!' && s[i + 1] !== '=') break;
      i++;
    }
    const term = s.slice(st, i).trim();
    return term ? evalCond(term, vars) : false;
  }
  function unary() { ws(); if (s[i] === '!' && s[i + 1] !== '=') { i++; return !unary(); } return primary(); }
  function and_() { let v = unary(); for (;;) { ws(); if (!eat('&&')) return v; v = unary() && v; } }
  function or_() { let v = and_(); for (;;) { ws(); if (!eat('||')) return v; v = and_() || v; } }
  return or_();
}

// 深いコピー（JSON なので構造化コピーで足りる）
const cloneTree = (n) => JSON.parse(JSON.stringify(n));

// KaedeUI::SeedVarsFromTree — bindVar を持つ部品の初期値を、未設定のキーだけ vars へ入れる
function seedVars(n, vars) {
  if (!n) return;
  if (n.bindVar && !(n.bindVar in vars)) {
    vars[n.bindVar] = n.type === 'Slider' ? (n.value || 0) : n.type === 'Toggle' ? (n.on ? 1 : 0)
                    : n.type === 'ToggleGroup' ? (n.selected || 0) : 0;
  }
  for (const k of (n.children || [])) seedVars(k, vars);
}
// KaedeUI::ApplyVarsToTree — vars の値を bindVar の部品へ書き戻す（木を書き換える）
function applyVars(n, vars) {
  if (!n) return;
  if (n.bindVar && n.bindVar in vars) {
    const v = vars[n.bindVar];
    if (n.type === 'Slider') n.value = v;
    else if (n.type === 'Toggle') n.on = v !== 0;
    else if (n.type === 'ToggleGroup') n.selected = Math.round(v);
  }
  for (const k of (n.children || [])) applyVars(k, vars);
}
function findById(n, id) {
  if (!n) return null;
  if (n.id === id) return n;
  for (const k of (n.children || [])) { const f = findById(k, id); if (f) return f; }
  return null;
}
// 画面JSON の Label へ文字を差し込む（作品名・スロット見出し・エンディング文言・toast）
function setLabel(root, id, text) { const n = findById(root, id); if (n) n.text = text; }
// KaedeUI::SetActionEnabled — その action を持つ部品の enabled をそろえる
function setActionEnabled(n, action, enabled) {
  if (!n) return;
  if (n.action === action) n.enabled = enabled;
  for (const k of (n.children || [])) setActionEnabled(k, action, enabled);
}
// KaedeCore::ApplyEnabledTree + PruneScreenTree — enabledIf/highlightIf を評価し、showIf/hideIf で刈る（木を書き換える）
function pruneTree(n, cond) {
  if (!n) return;
  if (n.enabledIf) n.enabled = evalCondExpr(n.enabledIf, cond);
  if (n.highlightIf) n.highlight = evalCondExpr(n.highlightIf, cond);
  if (n.anim && n.anim.pulse) n._pulseOn = !n.anim.pulse.whileIf || evalCondExpr(n.anim.pulse.whileIf, cond);
  if (n.children) {
    n.children = n.children.filter((k) =>
      !(k.hideIf && evalCondExpr(k.hideIf, cond)) && !(k.showIf && !evalCondExpr(k.showIf, cond)));
    for (const k of n.children) pruneTree(k, cond);
  }
}

// ---- KaedeCore::BindScreenTree の移植（engine/ui/Interp.cpp の expandRepeats / interpolate・2026-10-03）----
// 1) repeat 展開: template を要素ごとに複製し、{item.<key>} を text/action/id/sprite/enabledIf/highlightIf/showIf/hideIf へ
// 2) グローバルの {key} 補間（文字列を先に、無ければ数値）: text/sprite/action。**未知のキーはそのまま残す**
// 3) は呼ぶ側が pruneTree(tree, cond) で（enabledIf/highlightIf の評価と showIf/hideIf の刈り込み）
function interpolate(s, resolve) {
  if (typeof s !== 'string' || s.indexOf('{') < 0) return s;
  return s.replace(/\{([^{}]*)\}/g, (all, key) => { const v = resolve(key); return v === undefined ? all : v; });
}
const ITEM_FIELDS = ['text', 'action', 'id', 'sprite', 'enabledIf', 'highlightIf', 'showIf', 'hideIf'];
function interpolateItem(n, resolve) {
  for (const f of ITEM_FIELDS) if (n[f]) n[f] = interpolate(n[f], resolve);
  for (const k of (n.children || [])) interpolateItem(k, resolve);
}
// 要素ごとに型を選ぶ（F2）: "templates": {"<種類>": {…}} と "templateKey"。item[templateKey] の型、無ければ template
function expandRepeats(n, lists) {
  if (n.repeat && (n.template || n.templates)) {
    const items = lists[n.repeat];
    if (Array.isArray(items)) {
      n.children = items.map((scope) => {
        const key = n.templateKey ? scope[n.templateKey] : undefined;
        const tpl = (n.templates && key !== undefined && n.templates[key]) || n.template;
        if (!tpl) return null;
        const clone = cloneTree(tpl);
        interpolateItem(clone, (key) => (key.startsWith('item.') && (key.slice(5) in scope) ? String(scope[key.slice(5)]) : undefined));
        expandRepeats(clone, lists);
        return clone;
      }).filter(Boolean);
    }
    delete n.repeat;
  } else {
    for (const k of (n.children || [])) expandRepeats(k, lists);
  }
}
// template を要素 1 つで展開する（repeat の 1 行ぶん。高さを測るときに host が使う。KaedeCore::InstantiateTemplate と同じ）
function instantiateTemplate(tpl, item) {
  const clone = cloneTree(tpl);
  interpolateItem(clone, (key) => (key.startsWith('item.') && (key.slice(5) in item) ? String(item[key.slice(5)]) : undefined));
  return clone;
}
function interpolateTree(n, resolve) {
  for (const f of ['text', 'sprite', 'action']) if (n[f]) n[f] = interpolate(n[f], resolve);
  for (const k of (n.children || [])) interpolateTree(k, resolve);
}
const formatNum = (v) => (Number.isInteger(v) ? String(v) : String(Math.round(v * 1000) / 1000));
function bindScreenTree(root, strVars, numVars, lists) {
  expandRepeats(root, lists || {});
  interpolateTree(root, (key) => (key in strVars ? String(strVars[key])
    : (typeof numVars[key] === 'number' ? formatNum(numVars[key]) : undefined)));
}

// 画面を出す入口（利用者向け・2026-10-03。UE の KaedeUI::PresentScreen と同じ形）:
// tree（元の木。書き換えない）の写しへ values（{strings, vars, lists}）を差し込み、条件で刈り、描く。
// 状態が変わったらもう一度呼ぶ。描いた写しを返す（host が文字欄の位置などを引くとき用）
// 値を差し込んで条件で刈った写しを作る（仕様 §6 の 1〜4。描かない。計測だけしたいときにも使う）
function prepare(tree, values) {
  const v = values || {};
  const vars = Object.assign({}, v.vars || {});
  const copy = cloneTree(tree);
  applyVars(copy, vars);
  bindScreenTree(copy, v.strings || {}, vars, v.lists || {});
  pruneTree(copy, vars);
  return copy;
}
function present(renderer, tree, values, bg) {
  const copy = prepare(tree, values);
  renderer.draw(copy, bg);
  return copy;
}

// ---- ブラウザの入力の受け持ち（attach・2026-10-03・仕様 §7「操作層」）----
// ポインタ（押す・ホイール・スクロール条のつまみ・指でのスクロール・Slider のドラッグ）、方向キーと Enter/Space、
// 文字欄の <input>（位置・例文・無効・maxLen・消えたら隠す・IME の変換中の Enter は送らない）を一手に受け持つ。
// host は値を渡して描く（render）ことと、押されたときの判断（onEvent）だけ。UE の SKaedeUiCanvas が受け持つ範囲と同じ。
//   const host = MomijiUICanvas.attach(renderer, canvas, {
//     stage,            // <input> を置く親（canvas と同じ座標系。省略は canvas の親）
//     render,           // 描き直す関数（host が MomijiUICanvas.present を呼ぶ）
//     onEvent(ev),      // { kind: press|submit|slider|toggle, action, nodeId, key, value, relX }
//                       //   value: submit は欄の文字列・slider は 0〜1 の数・toggle は切り替えたあとの真偽（Toggle）か押した項目の番号（ToggleGroup）・press は無し
//     fit,              // true なら窓の大きさに合わせる: stage を CSS の scale で拡大縮小し、canvas の画素数を 拡大率 × devicePixelRatio にして描き直す（resize も受ける）
//     drafts,           // 文字欄の中身の置き場（{ [bindVar]: 文字列 }）。**渡せば host の所有**＝画面をまたいで同じ器・寿命は host が決める（明示的な共有）。
//                       // 省略すれば MomijiUI の内部＝**画面（screenId）ごと**に別（別の画面の同じ bindVar と混ざらない。R.resetScreen で捨てる）
//     animate,          // 動き（anim）がある間 render を呼び続ける（既定 true）
//   });
//   host.inputValue(bindVar) / host.setInputValue(bindVar, v) / host.focusInput(bindVar) / host.hasInput(bindVar) / host.detach()
//
// 文字欄の編集器（<input>）は、配置・描画・当たりと**同じ操作スコープとクリップ**を使う（2026-10-04・Codex 10/04/01 の F1/F2）:
//   操作できる欄 = 押せる ∧ 最前面の群（Modal の中なら Modal の）∧ ScrollView の見える部分がある（R.inputInteractive）。
//   そうでない欄は <input> を無効にし、編集中なら降りる（Modal を開いた・隠れた・押せなくなった）。見える部分だけをクリップ用の親で切る＝枠外は描かれず押せない。
//   Enter は送る直前にもう一度確かめる。
function attach(R, canvas, opts) {
  opts = opts || {};
  // attach の持ち物（下の attachInputs / attachPointer / attachKeys が分け合う）
  const A = { R: R, canvas: canvas, opts: opts, stage: opts.stage || canvas.parentElement,
              inputs: new Map(),   // bindVar -> { el: <input>, box: クリップ用の親 <div> }
              raf: 0, drag: null, detached: false, lastScreen: R.screenId, copiedTimer: 0 };
  A.draftsOf = () => opts.drafts || R.screenState().drafts;
  A.render = () => { if (!A.detached && opts.render) opts.render(); };
  A.toLogical = (ev) => { const r = canvas.getBoundingClientRect(); return { x: (ev.clientX - r.left) / r.width * R.vpW, y: (ev.clientY - r.top) / r.height * R.vpH }; };
  const kindOf = (h) => (h.type === 'Slider' ? 'slider' : (h.type === 'Toggle' || h.type === 'ToggleGroup') ? 'toggle' : h.type === 'TextInput' ? 'submit' : 'press');
  // 事象の value は部品の種類で決まる（host が座標や relX から値を復元しない・仕様 §6「操作層」）
  const valueOf = (h, relX, kind) => (kind === 'submit' ? (A.draftsOf()[h.bindVar] || '') : kind === 'slider' ? relX
    : kind === 'toggle' ? (h.type === 'ToggleGroup' ? (h.index || 0) : !h.on) : undefined);
  A.emit = (h, relX, kind) => { const k = kind || kindOf(h); if (opts.onEvent) opts.onEvent({ kind: k, action: h.action, nodeId: h.id || '', key: h.key, value: valueOf(h, relX, k), relX: relX }); };
  A.inputOf = (bindVar) => R.textInputs.find((x) => x.bindVar === bindVar);

  const syncInputs = attachInputs(A);
  // 描く前と後の受け口（R.draw が呼ぶ）: 画面が変わったら編集器を降り、描いたあとに欄を揃え、動きがある間は描き直し続ける
  const hook = {
    beforeDraw() {
      if (R.screenId === A.lastScreen) return;   // 画面が変わる: 編集器は前の画面のものなので降りる（新しい画面の下書きを <input> へ入れ直すため）
      A.lastScreen = R.screenId;
      for (const it of A.inputs.values()) if (document.activeElement === it.el) dropEditor(it.el);
    },
    afterDraw() {
      syncInputs();
      if (opts.animate !== false && R.pulsing && !A.raf) A.raf = requestAnimationFrame(() => { A.raf = 0; A.render(); });
    },
  };
  R._host = hook;
  const listeners = attachPointer(A).concat(attachKeys(A));
  for (const l of listeners) l[0].addEventListener(l[1], l[2], l[3]);
  // 窓に合わせる（fit）: 論理 vpW×vpH を縦横比のまま拡大し、高 DPI ではくっきり描く。論理座標→ページ座標の変換は toLogical（getBoundingClientRect）が受ける
  const onResize = () => {
    const s = Math.min(window.innerWidth / R.vpW, window.innerHeight / R.vpH);
    A.stage.style.transformOrigin = '0 0';
    A.stage.style.transform = 'translate(' + ((window.innerWidth - R.vpW * s) / 2) + 'px, ' + ((window.innerHeight - R.vpH * s) / 2) + 'px) scale(' + s + ')';
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(R.vpW * s * dpr);
    canvas.height = Math.round(R.vpH * s * dpr);
    R.setScale(s * dpr);
    A.render();
  };
  if (opts.fit) { window.addEventListener('resize', onResize); onResize(); }

  const inputs = A.inputs;
  return {
    inputValue: (bindVar) => A.draftsOf()[bindVar] || '',
    setInputValue(bindVar, v) { const d = A.draftsOf(); d[bindVar] = v || ''; const it = inputs.get(bindVar); if (it) it.el.value = d[bindVar]; },
    hasInput: (bindVar) => { const it = inputs.get(bindVar); return !!it && it.box.style.display !== 'none'; },
    // 画面に出ていて操作できる欄（最前面の群・見える・押せる）だけ。別の欄を打っている最中は奪わない（steal: true で奪う）
    focusInput(bindVar, o) {
      const it = inputs.get(bindVar), t = A.inputOf(bindVar);
      if (!it || !t || !R.inputInteractive(t) || it.el.disabled) return false;
      const a = document.activeElement;
      if (a && a.tagName === 'INPUT' && a !== it.el && !(o && o.steal)) return false;
      it.el.focus({ preventScroll: true });
      return true;
    },
    // いま編集している欄の bindVar（無ければ空）。検証の口
    activeInput: () => { for (const [k, it] of inputs) if (document.activeElement === it.el) return k; return ''; },
    syncInputs,
    fit: onResize,   // 窓に合わせ直す（fit を使わない host が自分の都合で呼んでもよい）
    // 入力の受け持ちをやめる: 聞いていた事象・<input>・描き直しの予約・描く前後の受け口を全部外す。描画器はそのまま
    detach() {
      A.detached = true;
      if (R._host === hook) R._host = null;
      if (A.raf) { cancelAnimationFrame(A.raf); A.raf = 0; }
      clearTimeout(A.copiedTimer);
      for (const l of listeners) l[0].removeEventListener(l[1], l[2], l[3]);
      if (opts.fit) window.removeEventListener('resize', onResize);
      for (const it of inputs.values()) it.box.remove();
      inputs.clear();
    },
    // 画面ごと壊す: detach に加えて描画器の持ち物（画像・計測の控え・動き・画面の状態）も捨てる（OSS 計画 §4「終了」）
    dispose() { this.detach(); R.dispose(); },
  };
}

// 編集を降りる: blur だけだと Chrome は欄の中の選択範囲を持ち続け、IME の確定文字（insertText）が焦点の無い欄へ入ってしまう
//（2026-10-04 に DevTools の Input.insertText で実測）。選択も消す
function dropEditor(el) {
  el.blur();
  try { const sel = window.getSelection(); if (sel) sel.removeAllRanges(); } catch (e) { /* 無い環境もある */ }
}

// 文字欄: MomijiUI が教える位置（R.textInputs）に <input> を重ねる。中身は drafts が持つ（描き直しても消えない）。揃える関数を返す
function attachInputs(A) {
  const R = A.R;
  const make = (t) => {
    const box = document.createElement('div');
    const el = document.createElement('input');
    el.type = 'text'; el.autocomplete = 'off'; el.spellcheck = false;
    el.dataset.bindVar = t.bindVar;   // 検証が elementFromPoint で欄を見分ける
    el.value = A.draftsOf()[t.bindVar] || '';
    el.addEventListener('input', () => { A.draftsOf()[t.bindVar] = el.value; });
    el.addEventListener('keydown', (ev) => {
      if (ev.key !== 'Enter' || ev.isComposing || ev.keyCode === 229) return;   // IME の変換中の Enter は確定であって送信ではない
      ev.preventDefault();
      A.draftsOf()[t.bindVar] = el.value;
      // 送る直前にもう一度確かめる: いま描かれているその欄が操作できる（最前面の群・見える・押せる）ときだけ（F1）
      const now = A.inputOf(t.bindVar);
      if (!now || !now.action || el.disabled || !R.inputInteractive(now)) return;
      A.emit({ type: 'TextInput', action: now.action, id: now.id, key: hitKey(now), bindVar: t.bindVar }, 0, 'submit');
    });
    box.appendChild(el);
    A.stage.appendChild(box);
    const it = { el: el, box: box };
    A.inputs.set(t.bindVar, it);
    return it;
  };
  return function syncInputs() {
    const drafts = A.draftsOf();
    const seen = new Set();
    for (const t of R.textInputs) {
      if (!t.bindVar || seen.has(t.bindVar)) continue;
      seen.add(t.bindVar);
      const it = A.inputs.get(t.bindVar) || make(t);
      const el = it.el, box = it.box;
      const ok = R.inputInteractive(t);
      el.placeholder = t.placeholder || '';
      el.maxLength = t.maxLen > 0 ? t.maxLen : 524288;
      if (!ok && document.activeElement === el) dropEditor(el);   // 背面になった・隠れた・押せなくなった欄は編集を降りる
      el.disabled = !ok;
      if (document.activeElement !== el && el.value !== (drafts[t.bindVar] || '')) el.value = drafts[t.bindVar] || '';
      // クリップ用の親は見える部分（t.clip）だけ。ポインタは親を素通りし（canvas へ）、<input> の見えている部分だけが受ける
      const cl = t.clip || { x: t.x, y: t.y, w: 0, h: 0 };
      Object.assign(box.style, { position: 'absolute', overflow: 'hidden', pointerEvents: 'none', margin: '0', padding: '0', border: '0',
        left: cl.x + 'px', top: cl.y + 'px', width: cl.w + 'px', height: cl.h + 'px', display: t.clip ? '' : 'none' });
      Object.assign(el.style, { position: 'absolute', boxSizing: 'border-box', margin: '0', pointerEvents: 'auto',
        left: (t.x - cl.x) + 'px', top: (t.y - cl.y) + 'px', width: t.w + 'px', height: t.h + 'px',
        fontSize: Math.round(t.fontSize * SLATE_PT_TO_PX) + 'px', display: '' });
    }
    for (const [k, it] of A.inputs) if (!seen.has(k)) { if (document.activeElement === it.el) dropEditor(it.el); it.box.style.display = 'none'; it.el.disabled = true; }   // 画面から消えた欄は隠す（打鍵を飲まない）
  };
}

// 文字の選択（2026-10-07・設計 10/07/01 §4.3・§4.7）: 選んで離した瞬間にクリップボードへ。navigator.clipboard が無い環境（file:// の下描き）は
// 隠した <textarea> と execCommand('copy')。焦点は元へ戻す（入力欄を打っている途中でも貼り付けへ戻れるように）
function copyText(text) {
  const fallback = () => {
    try {
      const prev = document.activeElement;
      const ta = document.createElement('textarea');
      ta.value = text; ta.setAttribute('readonly', '');
      Object.assign(ta.style, { position: 'fixed', left: '-9999px', top: '0', opacity: '0' });
      document.body.appendChild(ta); ta.select();
      document.execCommand('copy');
      ta.remove();
      if (prev && prev.focus) prev.focus({ preventScroll: true });
    } catch (e) { /* コピーできない環境（権限なし）でも選択は残る */ }
  };
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) { navigator.clipboard.writeText(text).catch(fallback); return; }
  } catch (e) { /* 下へ */ }
  fallback();
}
// 押した位置の部品の中の横の比（Slider の値）
const relX = (h, p) => (h.w > 0 ? Math.max(0, Math.min(1, (p.x - h.x) / h.w)) : 0);

// ポインタ（押す・ホイール・スクロール条のつまみ・指でのスクロール・Slider のドラッグ・文字の選択）。聞く事象の表 [対象, 名前, 受け手, 設定] を返す
function attachPointer(A) {
  const R = A.R, canvas = A.canvas, render = A.render;
  const scrollAreaAt = (p) => { for (let i = R.scrollAreas.length - 1; i >= 0; i--) { const a = R.scrollAreas[i]; if (p.x >= a.x && p.x < a.x + a.w && p.y >= a.y && p.y < a.y + a.h && a.contentH > a.h) return a; } return null; };
  const setHover = (p) => {   // ホバー（マウス操作中だけ・焦点の枠が出ているときは消す）
    const h = p ? R.hitAt(p.x, p.y) : null;
    const key = h && h.focusable ? h.key : '';
    if (key !== R.hoverKey) { R.hoverKey = key; R.hoverT0 = performance.now(); render(); }
  };
  const onDown = (ev) => {
    if (ev.button !== 0 && ev.pointerType === 'mouse') return;
    const p = A.toLogical(ev);
    const area = scrollAreaAt(p);
    const hit = R.hitAt(p.x, p.y);
    const drag = A.drag = { p, area, hit, startY: p.y, startOff: area ? (R.scroll.get(area.id) || 0) : 0, mode: '', moved: false };
    if (hit && hit.action && hit.type !== 'Modal') { R.pressedKey = hit.key; render(); }
    const thumb = area && p.x >= area.x + area.w - 14;
    // 選べる器の中（押せる当たり・スクロール条の上ではない）をマウスで押した: 押した点を控える（4px 動けば選択・動かずに離せば従来のクリック）。指は従来どおり中身のスクロール
    const selGroup = !thumb && ev.pointerType === 'mouse' ? R.selAreaAt(p.x, p.y) : '';
    if (thumb) drag.mode = 'thumb';            // スクロール条をつまむ
    else if (selGroup) { drag.mode = 'selpend'; drag.selGroup = selGroup; }
    else if (hit && hit.type === 'Slider') { drag.mode = 'slider'; A.emit(hit, relX(hit, p)); }   // Slider は押した位置の比
    else if (area && ev.pointerType !== 'mouse') drag.mode = 'content';      // 指で中身をなぞる
    if (!thumb && !selGroup && R.clearSelection()) render();   // 器の外・当たりを押した（§4.4）
    if (drag.mode) canvas.setPointerCapture(ev.pointerId);
    if (R.focusVisible) { R.focusVisible = false; render(); }
    ev.preventDefault();
  };
  // 文字の選択のドラッグ中（成立前の 4px・選んでいる最中・描き直しで行が変わって死んだ）
  const moveSelect = (drag, p) => {
    if (drag.mode === 'seldead') return;
    if (drag.mode === 'selpend') {
      if (Math.hypot(p.x - drag.p.x, p.y - drag.p.y) < 4) return;
      drag.mode = 'select';   // 成立: 押した点を anchor（押した器の中で解く）
      R.selBegin(drag.selGroup, drag.p.x, drag.p.y);
      canvas.style.cursor = 'text';
    } else if (!R.selection) { drag.mode = 'seldead'; return; }   // 描き直しで選んでいた行が変わった（§4.4）。離すまで何もしない
    if (R.selExtend(p.x, p.y)) render();
  };
  const onMove = (ev) => {
    if (R.focusVisible) { R.focusVisible = false; render(); }
    const p = A.toLogical(ev), drag = A.drag;
    // 文字の選択のドラッグ中は、それだけを見る（ホバーも動かさない）
    if (drag && (drag.mode === 'selpend' || drag.mode === 'select' || drag.mode === 'seldead')) { moveSelect(drag, p); return; }
    if (!drag && ev.pointerType === 'mouse') { const want = R.selTextAt(p.x, p.y) ? 'text' : ''; if (canvas.style.cursor !== want) canvas.style.cursor = want; }   // 選べる行の上は I ビーム
    if (!drag || drag.mode !== 'thumb') setHover(p);   // つまみを掴んでいる間はホバーを動かさない（UE と同じ）
    if (!drag || !drag.mode) return;
    if (drag.mode === 'slider') { A.emit(drag.hit, relX(drag.hit, p)); return; }
    const dy = p.y - drag.startY;
    if (Math.abs(dy) > 6) drag.moved = true;
    if (!drag.moved) return;
    const a = drag.area, maxOff = Math.max(0, a.contentH - a.h);
    const off = drag.mode === 'thumb' ? drag.startOff + dy * (a.contentH / a.h) : drag.startOff - dy;
    R.scroll.set(a.id, Math.max(0, Math.min(maxOff, off)));
    render();
  };
  // 選んで離した: 範囲が空なら消し、文字があれば写して「コピーしました」の札
  const endSelect = (d, p) => {
    if (d.mode === 'select' && R.selection) {
      R.selExtend(p.x, p.y);
      const sel = R.selection;
      if (sel.from[0] === sel.to[0] && sel.from[1] === sel.to[1]) R.clearSelection();   // 範囲が空なら何もしない
      else if (sel.text) {
        copyText(sel.text);
        R.copiedAt = performance.now(); R.copiedPos = { x: p.x, y: p.y };
        clearTimeout(A.copiedTimer);
        A.copiedTimer = setTimeout(render, 950);   // 札を消す
      }
    }
    render();
  };
  const onUp = (ev) => {
    const d = A.drag; A.drag = null;
    if (R.pressedKey) { R.pressedKey = ''; render(); }
    if (d && (d.mode === 'select' || d.mode === 'seldead')) { endSelect(d, A.toLogical(ev)); return; }
    if (d && d.mode === 'selpend') {   // 4px 動かずに離した＝従来どおりのクリック（押した点に押せる当たりは無い）。選択は消える
      if (R.clearSelection()) render();
      return;
    }
    if (!d || d.moved || d.mode === 'thumb' || d.mode === 'slider') return;
    const p = A.toLogical(ev);
    const hit = R.hitAt(p.x, p.y);
    if (!hit || !hit.action || !d.hit || hit.key !== d.hit.key) return;   // 押した部品の上で離したときだけ（UE と同じ。押下で描き直すので鍵で比べる）
    R.flashKey = hit.key; R.flashT0 = performance.now();   // クリック成立の光り（150ms）
    A.emit(hit, relX(hit, p));
    render();
  };
  const onCancel = () => { A.drag = null; if (R.pressedKey) { R.pressedKey = ''; render(); } };   // 選択のドラッグは途中で終わる（選んだ範囲は残す・コピーはしない）
  const onLeave = () => { if (R.hoverKey) { R.hoverKey = ''; render(); } };
  const onWheel = (ev) => { const p = A.toLogical(ev); if (R.wheelAt(p.x, p.y, ev.deltaY)) { ev.preventDefault(); render(); } };
  return [[canvas, 'pointerdown', onDown], [canvas, 'pointermove', onMove], [canvas, 'pointerup', onUp], [canvas, 'pointercancel', onCancel],
          [canvas, 'pointerleave', onLeave], [canvas, 'wheel', onWheel, { passive: false }]];
}

// 方向キーで焦点を動かし、Enter / Space で押す。文字欄を打っている間は文字欄に任せる
function attachKeys(A) {
  const R = A.R;
  const onKey = (ev) => {
    const typing = document.activeElement && document.activeElement.tagName === 'INPUT';
    if (typing) return;
    const dir = { ArrowUp: [0, -1], ArrowDown: [0, 1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] }[ev.key];
    if (dir) { R.moveFocus(dir[0], dir[1]); A.render(); ev.preventDefault(); return; }
    if ((ev.key === 'Enter' || ev.key === ' ') && R.focusVisible) { const h = R.focusedHit(); if (h) { A.emit(h, 0.5); ev.preventDefault(); } }
  };
  return [[document, 'keydown', onKey]];
}

// ---- 画面 JSON の診断（validate・2026-10-03・仕様 §12）----
// 未知の type・未知の鍵・型違い・列挙の外の値・静的な id の重複・repeat に型が無い を、場所（path）つきで返す。
// UE の kaedevn::ui::validateScreen と同じ規則（code と path が一致する。照合が突き合わせる）。
//   severity: "error"（描けない／意図どおりにならない）| "warn"（無視されるが描ける）
const V_NODE_KEYS = new Set(['type', 'id', 'x', 'y', 'w', 'h', 'gap', 'pad', 'margin', 'cols', 'flex', 'minW', 'minH', 'anchor',
  'showIf', 'hideIf', 'highlightIf', 'enabledIf', 'text', 'action', 'variant', 'enabled', 'focusable', 'center', 'highlight',
  'color', 'textColor', 'borderColor', 'borderWidth', 'align', 'font', 'fontSize', 'wrapW', 'lineH', 'ellipsis', 'runs', 'placeholder', 'bindVar', 'maxLen',
  'focused', 'sprite', 'fit', 'icon', 'value', 'on', 'selected', 'dim', 'points', 'thickness',
  'closed', 'fill', 'anim', 'repeat', 'template', 'templates', 'templateKey', 'children', 'selectable',
  'gradient', 'shape', 'shapeInset', 'edge3d', 'innerBorder', 'slice', 'tile', 'src', 'ornament', 'textOutline', 'textGlow', 'letterSpacing',
  'monoDigits', 'highlightTextColor', 'offset', 'rotate', 'skewX', 'clip', 'opacity', 'glow', 'backdropBlur']);
const V_DOC_KEYS = new Set(['id', 'note', 'title', 'root', 'bind', 'scenarios', 'invalid', 'host']);   // host: 画面を使う側の数値（描画器は見ない）
const V_NUM = new Set(['x', 'y', 'w', 'h', 'gap', 'pad', 'margin', 'cols', 'flex', 'minW', 'minH', 'fontSize', 'wrapW', 'lineH', 'maxLen', 'value', 'selected', 'thickness', 'borderWidth', 'shapeInset', 'letterSpacing', 'rotate', 'skewX', 'opacity', 'backdropBlur']);
const V_STR = new Set(['type', 'id', 'anchor', 'showIf', 'hideIf', 'highlightIf', 'enabledIf', 'text', 'action', 'variant', 'color', 'textColor', 'borderColor', 'highlightTextColor', 'shape', 'ornament', 'align', 'font', 'placeholder', 'bindVar', 'sprite', 'fit', 'icon', 'repeat', 'templateKey']);
const V_BOOL = new Set(['enabled', 'focusable', 'center', 'highlight', 'selectable', 'ellipsis', 'focused', 'on', 'closed', 'fill', 'tile', 'monoDigits', 'clip']);
const V_TYPES = new Set(['Panel', 'Image', 'Label', 'Button', 'Slider', 'Toggle', 'ToggleGroup', 'ScrollView', 'Modal', 'VerticalLayout', 'HorizontalLayout', 'GridLayout', 'TextInput', 'Polyline', 'Ellipse', 'Polygon', 'Divider']);
const V_ENUM = { anchor: ['fill', 'safe', 'top', 'bottom', 'left', 'right', 'center'], fit: ['contain', 'cover', 'stretch'], align: ['left', 'center', 'right'] };
const V_ANIM_KEYS = new Set(['enter', 'countUp', 'pulse', 'staggerMs', 'blink', 'bob', 'frames', 'exit', 'tween']);
const V_COLOR = /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/;
function validate(doc) {
  const out = [];
  const push = (severity, code, path, message) => out.push({ severity, code, path, message });
  const ids = new Map();
  const isObj = (v) => v && typeof v === 'object' && !Array.isArray(v);
  const node = (n, path) => {
    if (!isObj(n)) { push('error', 'bad-value', path, '部品はオブジェクト'); return; }
    const type = n.type === undefined ? 'Panel' : n.type;
    if (typeof type !== 'string' || !V_TYPES.has(type)) push('error', 'unknown-type', path + '/type', `知らない type: ${JSON.stringify(n.type)}`);
    for (const k of Object.keys(n)) {
      if (k.startsWith('_')) continue;   // 注記
      const v = n[k], p = path + '/' + k;
      if (!V_NODE_KEYS.has(k)) { push('warn', 'unknown-key', p, `知らない鍵（無視される）: ${k}`); continue; }
      if (V_NUM.has(k) && typeof v !== 'number') push('error', 'bad-value', p, `${k} は数`);
      else if (V_STR.has(k) && typeof v !== 'string') push('error', 'bad-value', p, `${k} は文字列`);
      else if (V_BOOL.has(k) && typeof v !== 'boolean') push('error', 'bad-value', p, `${k} は真偽`);
      else if (k === 'dim' && typeof v !== 'boolean' && typeof v !== 'number') push('error', 'bad-value', p, 'dim は真偽か数');
      else if ((k === 'children' || k === 'runs' || k === 'points') && !Array.isArray(v)) push('error', 'bad-value', p, `${k} は配列`);
      else if ((k === 'template' || k === 'templates' || k === 'anim') && !isObj(v)) push('error', 'bad-value', p, `${k} はオブジェクト`);
      else if (V_ENUM[k] && typeof v === 'string' && !V_ENUM[k].includes(v)) push('error', 'bad-value', p, `${k} は ${V_ENUM[k].join('|')}`);
      else if ((k === 'color' || k === 'textColor' || k === 'borderColor') && typeof v === 'string' && !v.startsWith('$') && !V_COLOR.test(v)) push('warn', 'bad-color', p, `色は #rrggbb か #rrggbbaa（無視されて既定の色になる）: ${v}`);
    }
    // selectable は器（Panel / VerticalLayout / ScrollView）だけ。ほかに書いても選べない（2026-10-07）
    if ('selectable' in n && !SEL_TYPES.has(type)) push('warn', 'selectable-ignored', path + '/selectable', 'selectable は Panel / VerticalLayout / ScrollView だけ（無視される）');
    if (typeof n.id === 'string' && n.id && !n.id.includes('{')) {
      // 状態を持つ部品（押せる・文字欄・ScrollView・Modal）の id の重複は焦点やスクロール量の鍵が曖昧になる＝error。見た目だけの部品は warn
      const stateful = !!n.action || !!n.bindVar || type === 'ScrollView' || type === 'Modal';
      const prev = ids.get(n.id);
      if (prev) push(stateful || prev.stateful ? 'error' : 'warn', 'dup-id', path + '/id', `id が重複: ${n.id}（先: ${prev.path}）`);
      else ids.set(n.id, { path, stateful });
    }
    if (typeof n.repeat === 'string' && n.repeat && !isObj(n.template) && !isObj(n.templates)) push('error', 'template-missing', path + '/repeat', 'repeat には template か templates が要る');
    if (typeof n.templateKey === 'string' && !isObj(n.templates)) push('error', 'template-missing', path + '/templateKey', 'templateKey には templates が要る');
    if (Array.isArray(n.runs)) n.runs.forEach((r, i) => { if (!isObj(r) || typeof r.text !== 'string') push('error', 'bad-value', `${path}/runs/${i}`, 'runs の要素は {text, bold?, color?, ruby?}'); });
    if (Array.isArray(n.points)) n.points.forEach((pt, i) => { if (!Array.isArray(pt) || pt.length < 2 || typeof pt[0] !== 'number' || typeof pt[1] !== 'number') push('error', 'bad-value', `${path}/points/${i}`, 'points の要素は [x, y]'); });
    if (isObj(n.anim)) {
      for (const k of Object.keys(n.anim)) if (!k.startsWith('_') && !V_ANIM_KEYS.has(k)) push('warn', 'unknown-key', `${path}/anim/${k}`, `anim の知らない鍵（無視される）: ${k}`);
      if (isObj(n.anim.enter) && typeof n.anim.enter.type === 'string' && !['fade', 'slide', 'pop'].includes(n.anim.enter.type)) push('error', 'bad-value', `${path}/anim/enter/type`, 'enter.type は fade|slide|pop');
    }
    if (isObj(n.template)) node(n.template, path + '/template');
    if (isObj(n.templates)) for (const k of Object.keys(n.templates)) node(n.templates[k], `${path}/templates/${k}`);
    if (Array.isArray(n.children)) n.children.forEach((c, i) => node(c, `${path}/children/${i}`));
  };
  if (!isObj(doc)) { push('error', 'bad-value', '', '画面はオブジェクト'); return out; }
  if (isObj(doc.root)) {
    for (const k of Object.keys(doc)) if (!k.startsWith('_') && !V_DOC_KEYS.has(k)) push('warn', 'unknown-key', '/' + k, `知らない鍵（無視される）: ${k}`);
    node(doc.root, '/root');
  } else node(doc, '');   // 裸の部品
  return out;
}

// 画面 JSON を読む（仕様 §12）: 診断して、error があれば**止める**（旧い画面のまま黙って続けない）。
// 戻り値 { root, host, diagnostics }。strict: false なら error があっても返す（diagnostics で見る）
function load(doc, opts) {
  const d = typeof doc === 'string' ? JSON.parse(doc) : doc;
  const diagnostics = validate(d);
  const errors = diagnostics.filter((x) => x.severity === 'error');
  if (errors.length && !(opts && opts.strict === false)) {
    const e = new Error('画面 JSON に誤りが ' + errors.length + ' 件: ' + errors.slice(0, 5).map((x) => x.path + ' ' + x.message).join(' / '));
    e.diagnostics = diagnostics;
    throw e;
  }
  return { root: d && d.root ? d.root : d, host: d && d.host ? d.host : null, diagnostics: diagnostics };
}
// 同梱書体（BIZ UDP ゴシック。@font-face の family は FONT_FAMILY）を読み終えるのを待つ。読めなければ false（OS の書体のまま描ける）。
// 描く前に待つ＝読む前に測った幅で折り返すと、届いたあとに行が重なる
const FONT_FAMILY = 'KaedeGothic';
async function loadFonts(faces) {
  if (typeof document === 'undefined' || !document.fonts) return false;
  const list = faces || ['16px "' + FONT_FAMILY + '"', '700 16px "' + FONT_FAMILY + '"'];
  try { await Promise.all(list.map((f) => document.fonts.load(f))); return list.every((f) => document.fonts.check(f)); } catch (e) { return false; }
}

const MomijiUICanvas = { Renderer: Renderer, THEME: MIDNIGHT, measureStackH: measureStackH,
  evalCondExpr: evalCondExpr, cloneTree: cloneTree, seedVars: seedVars, applyVars: applyVars,
  findById: findById, setLabel: setLabel, setActionEnabled: setActionEnabled, pruneTree: pruneTree,
  bindScreenTree: bindScreenTree, lineHFor: lineHFor, present: present, prepare: prepare,
  instantiateTemplate: instantiateTemplate, hitKey: hitKey, attach: attach, validate: validate, load: load, loadFonts: loadFonts, FONT_FAMILY: FONT_FAMILY,
  THEMES: { atelier: ATELIER, midnight: MIDNIGHT, sakura: SAKURA } };

export { MomijiUICanvas, Renderer, attach, validate, load, loadFonts, FONT_FAMILY, present, prepare, measureStackH, hitKey, instantiateTemplate, bindScreenTree, pruneTree, seedVars, applyVars, findById, setLabel, setActionEnabled, cloneTree, evalCondExpr, lineHFor };
export default MomijiUICanvas;
