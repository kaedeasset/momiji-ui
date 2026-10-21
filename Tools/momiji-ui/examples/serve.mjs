#!/usr/bin/env node
// MomijiUI の見本を開くための小さな静的サーバ（依存なし・Node 18 以上）。
//
//   node Tools/momiji-ui/examples/serve.mjs [port]          # 既定 8840 → http://localhost:8840/counter/（源から直接）
//   node Tools/momiji-ui/examples/serve.mjs --dist [port]   # 配布物 Tools/momiji-ui/web/dist/ をそのまま出す（先に node Tools/momiji-ui/web/build.mjs）
//                                                        #   → http://localhost:8840/examples/counter/
//   --lan を付けると同じ LAN の別の PC / Mac からも開ける（0.0.0.0 で待つ。既定は 127.0.0.1 だけ）
//
// 見本のフォルダ（Tools/momiji-ui/examples/）に加えて、MomijiUI 本体と書体と画面 JSON を見本から見える場所に出す
//（配布物 Tools/momiji-ui/web/dist/ と同じ並び。build.mjs を回さなくても源から直接出す）:
//   /momiji-ui.mjs        ← Tools/momiji-ui/web/src/momiji-ui.mjs（源・ES module）
//   /momiji-ui-canvas.js  ← Scripts/lib/momiji-ui-canvas.js（<script> で読む従来形・生成物）
//   /momiji-ui.css        ← build.mjs の標準スタイル
//   /fonts/*.ttf       ← Plugins/MomijiUI/Resources/Fonts/（BIZ UDP ゴシック・SIL OFL 1.1）
//   /counter.json      ← Plugins/MomijiUI/Examples/counter.json（UE の見本と同じファイル）
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CSS } from '../web/build.mjs';
import { ROOT, TOOL_DIR, SRC, CLASSIC, fontRoutes } from '../lib/paths.mjs';
import { serveStatic } from '../lib/static-server.mjs';

const HERE = path.join(TOOL_DIR, 'examples');
const DIST = path.join(TOOL_DIR, 'web', 'dist');
const ROUTES = Object.assign({
	'/momiji-ui.mjs': SRC,
	'/momiji-ui-canvas.js': CLASSIC,
	'/momiji-ui.css': { type: 'text/css; charset=utf-8', body: () => CSS },
	'/counter.json': path.join(ROOT, 'Plugins', 'MomijiUI', 'Examples', 'counter.json'),
}, fontRoutes());

export function serveExamples(port, dist, lan) {
	return dist
		? serveStatic(port, { resolve: (url) => path.join(DIST, url), lan, notFound: 'not found（先に node Tools/momiji-ui/web/build.mjs）' })   // 配布物をそのまま（利用者が受け取る形で確かめる）
		: serveStatic(port, { routes: ROUTES, resolve: (url) => path.join(HERE, url), lan });
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	const dist = process.argv.includes('--dist'), lan = process.argv.includes('--lan');
	const port = Number(process.argv.slice(2).find((a) => /^\d+$/.test(a)) || 8840);
	await serveExamples(port, dist, lan);
	console.log(dist ? `open http://localhost:${port}/examples/counter/（配布物 Tools/momiji-ui/web/dist/）` : `open http://localhost:${port}/counter/`);
}