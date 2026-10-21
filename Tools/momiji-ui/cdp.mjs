// momiji-ui/cdp.mjs — ヘッドレス Chrome を DevTools の口（WebSocket・Node 22 の組み込み）で動かす最小の道具。
// 使う側: Tools/momiji-ui/lib/driver.mjs（照合と見本の確かめの口）・Tools/adv-web/shots.mjs（ADV の画面を撮る）・Scripts/rpg-compare-mz.mjs。
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export async function waitFor(fn, ms, what) {
	const until = Date.now() + ms;
	for (;;) {
		try { const v = await fn(); if (v) return v; } catch { /* まだ */ }
		if (Date.now() > until) throw new Error(`待ちきれない: ${what}`);
		await sleep(150);
	}
}

// Chrome を 1280x720 で立ち上げ、ページ 1 枚の口を返す。close() で Chrome ごと止めて作業フォルダを消す
export async function openChrome(chromeExe, port) {
	const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'momiji-ui-chrome-'));
	const proc = spawn(chromeExe, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${tmp}`,
		'--no-first-run', '--no-default-browser-check', '--hide-scrollbars', '--window-size=1280,720', 'about:blank'], { stdio: 'ignore' });
	const targets = await waitFor(async () => (await fetch(`http://127.0.0.1:${port}/json`)).json(), 30000, 'chrome');
	const page = targets.find((t) => t.type === 'page');
	const ws = new WebSocket(page.webSocketDebuggerUrl);
	let id = 0;
	const pending = new Map();
	ws.onmessage = (ev) => {
		const m = JSON.parse(ev.data);
		if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result); }
	};
	await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('DevTools の口に繋がらない')); });
	// 切れたら（Chrome が落ちた・閉じた）待っている送信を全部失敗させ、以後の送信もすぐ失敗させる（切断で照合が黙って止まらないように）
	let lost = null;
	const fail = (why) => {
		if (lost) return;
		lost = new Error(`DevTools の接続が切れた（${why}）`);
		for (const p of pending.values()) p.reject(lost);
		pending.clear();
	};
	ws.onclose = (ev) => fail(`close ${ev.code}`);
	ws.onerror = () => fail('error');
	const send = (method, params = {}) => new Promise((resolve, reject) => {
		if (lost) { reject(lost); return; }
		const i = ++id; pending.set(i, { resolve, reject });
		try { ws.send(JSON.stringify({ id: i, method, params })); } catch (e) { pending.delete(i); reject(e); }
	});
	await send('Page.enable');
	await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
	const evalJs = async (expr) => {
		const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
		if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
		return r.result.value;
	};
	return {
		send, evalJs,
		navigate: (url) => send('Page.navigate', { url }),
		screenshot: async () => Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).data, 'base64'),
		async close() {
			try { ws.close(); } catch { /* 済み */ }
			proc.kill();
			await sleep(500);
			fs.rmSync(tmp, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 });
		},
	};
}
