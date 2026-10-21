// momiji-ui/pw.mjs — Playwright で Chromium・Firefox・WebKit を開き、照合（conformance.mjs）が使う同じ口（driver）を返す。
// Chrome の DevTools 直結（cdp.mjs）と同じ形: navigate / evalJs / click / key / type / imeSetComposition / imeClose / screenshot / close。
// 依存は Tools/momiji-ui/package.json（開発用。本体 momiji-ui.mjs は依存なし）。ブラウザ本体は `npx playwright install chromium firefox webkit`。
//
// 注意（計画 Docs/CodexReports/2026/10/04/02 §11）: Playwright の WebKit は製品版 Safari そのものではない。「WebKit PASS」と「Safari 確認済み」は別の欄。
import { createRequire } from 'node:module';

export const ENGINES = ['chromium', 'firefox', 'webkit'];

export async function openEngine(name) {
	if (!ENGINES.includes(name)) throw new Error(`知らないブラウザ: ${name}（chromium | firefox | webkit）`);
	const require = createRequire(import.meta.url);
	let pw;
	try { pw = require('playwright'); }
	catch (e) { throw new Error('playwright が無い。Tools/momiji-ui で npm install と npx playwright install chromium firefox webkit を先に（' + e.message + '）'); }
	const browser = await pw[name].launch({ headless: true });
	const context = await browser.newContext({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
	const page = await context.newPage();
	// 変換中の Enter（IME）は Chromium だけ DevTools の口で本物の composition を起こせる。ほかは null＝呼ぶ側が合成イベントに落ちる
	const cdp = name === 'chromium' ? await context.newCDPSession(page) : null;
	return {
		engine: name,
		version: browser.version(),
		navigate: (url) => page.goto(url),
		evalJs: (expr) => page.evaluate(expr),
		click: (x, y) => page.mouse.click(x, y),
		drag: async (x0, y0, x1, y1) => { await page.mouse.move(x0, y0); await page.mouse.down(); await page.mouse.move(x1, y1); await page.mouse.up(); },   // 文字の選択の手
		key: (keyName) => page.keyboard.press(keyName),
		type: (text) => page.keyboard.type(text),   // 1 字ずつ keydown / keypress / input（焦点の欄が無ければどこにも入らない）
		imeSetComposition: cdp ? async (text) => { await cdp.send('Input.imeSetComposition', { text, selectionStart: text.length, selectionEnd: text.length }); return true; } : null,
		imeClose: cdp ? () => cdp.send('Input.insertText', { text: '' }) : null,
		screenshot: () => page.screenshot({ type: 'png' }),
		close: () => browser.close(),
	};
}
