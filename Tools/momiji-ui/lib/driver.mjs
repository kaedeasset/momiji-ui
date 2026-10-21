// momiji-ui/lib/driver.mjs — ブラウザの口（driver）。照合（conformance.mjs）と見本の確かめ（examples/check.mjs）が同じ口で本物の入力を送る。
// Chrome は DevTools 直結（cdp.mjs を包む）、ほかは Playwright（pw.mjs が同じ形で返す）:
//   navigate / evalJs / click / drag / key / type / imeSetComposition / imeClose / screenshot / close（Chrome だけ viewport も・説明書の絵）
// 入力はページの座標（＝論理座標。窓 1280×720・等倍）で送る
import { openChrome } from '../cdp.mjs';
import { openEngine } from '../pw.mjs';

const KEYS = { ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, Enter: 13, Tab: 9 };

function cdpDriver(chrome) {
	return {
		engine: 'chrome', version: 'OS の Chrome',
		navigate: chrome.navigate, evalJs: chrome.evalJs, screenshot: chrome.screenshot, close: chrome.close,
		// 頁の大きさを変える（説明書の絵は 1920×1080。照合は 1280×720 のまま）
		viewport: (width, height) => chrome.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }),
		async key(name) { for (const type of ['keyDown', 'keyUp']) await chrome.send('Input.dispatchKeyEvent', { type, key: name, code: name, windowsVirtualKeyCode: KEYS[name] }); },
		// 打鍵は 1 字ずつキーボードの文字イベント（char）で送る。焦点の欄が無ければどこにも入らない（本物の打鍵と同じ）。
		// Input.insertText は打鍵に使わない: blur した欄にも入ってしまう（Chrome は欄の中の選択範囲を持ち続ける・2026-10-04 実測）
		async type(text) { for (const ch of text) await chrome.send('Input.dispatchKeyEvent', { type: 'char', text: ch }); },
		async click(x, y) { for (const type of ['mousePressed', 'mouseReleased']) await chrome.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 }); },
		// 押す→（押したまま）動かす→離す。文字の選択の手（2026-10-07）
		async drag(x0, y0, x1, y1) {
			await chrome.send('Input.dispatchMouseEvent', { type: 'mousePressed', x: x0, y: y0, button: 'left', buttons: 1, clickCount: 1 });
			await chrome.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: x1, y: y1, button: 'left', buttons: 1 });
			await chrome.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x: x1, y: y1, button: 'left', buttons: 0, clickCount: 1 });
		},
		async imeSetComposition(text) { await chrome.send('Input.imeSetComposition', { text, selectionStart: text.length, selectionEnd: text.length }); return true; },
		imeClose: () => chrome.send('Input.insertText', { text: '' }),   // 変換を閉じる（何も確定しない）
	};
}

// engine: chrome（OS の Chrome・chromeExe と port が要る）か chromium / firefox / webkit（Playwright）
export async function openDriver(engine, { chromeExe, port } = {}) {
	return engine === 'chrome' ? cdpDriver(await openChrome(chromeExe, port)) : openEngine(engine);
}
