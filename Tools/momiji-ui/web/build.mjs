#!/usr/bin/env node
// momiji-ui/web/build.mjs — ブラウザ版 MomijiUI の配布物を源（src/momiji-ui.mjs）から作る（依存なし・Node 18 以上）。
//
//   node Tools/momiji-ui/web/build.mjs            # dist/ を作り、Scripts/lib/momiji-ui-canvas.js（<script> で読む従来形）も書き直す
//   node Tools/momiji-ui/web/build.mjs --check    # 何も書かず、Scripts/lib/momiji-ui-canvas.js が源から作ったものと同じかだけ見る（照合が呼ぶ）
//
// dist/ の中身（これだけで UE も KaedeVN も無しに静的 HTTP 配信で動く）:
//   momiji-ui.mjs             ES module（源そのもの）
//   momiji-ui-canvas.js       <script src> で読む従来形（window.MomijiUICanvas）。同じ源から機械的に作る＝手書きの写しを持たない
//   momiji-ui.css             標準のスタイル（stage・canvas・文字欄の <input>）
//   fonts/                 同梱書体 BIZ UDP ゴシック（Regular / Bold・SIL OFL 1.1）と OFL の文
//   examples/counter/      最小の見本（index.html・main.js・counter.json）
//   README.md              配布物の説明
//
// 版: 配布物の頭の注釈（momiji-ui.mjs・momiji-ui-canvas.js・momiji-ui.css）と README.md に「MomijiUI <版>」を書く。版の正は UE の
// Plugins/MomijiUI/MomijiUI.uplugin の VersionName（UE とブラウザで同じ番号。package.json には書かない。設計 free-plugins-fab-docs-design.md §5.6）。
// Scripts/lib/momiji-ui-canvas.js（リポジトリの中で使う従来形）には書かない（版を上げるたびに中身の同じ生成物が変わらないように）。
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROOT, TOOL_DIR, SRC, CLASSIC, FONT_DIR, FONTS, fontFaceCss } from '../lib/paths.mjs';

const DIST = path.join(TOOL_DIR, 'web', 'dist');

// 版の正（UE の .uplugin の VersionName）。読めなければ止まる（版の無い配布物を作らない）
export function pluginVersion() {
	const v = JSON.parse(fs.readFileSync(path.join(ROOT, 'Plugins', 'MomijiUI', 'MomijiUI.uplugin'), 'utf8')).VersionName;
	if (!/^\d+\.\d+\.\d+$/.test(v || '')) throw new Error('Plugins/MomijiUI/MomijiUI.uplugin の VersionName が X.Y.Z の形でない: ' + v);
	return v;
}

// ES module → <script> で読む従来形。export 行を外し、関数で包んで window.MomijiUICanvas に置く
export function classicFrom(esm) {
	const body = esm
		.replace(/^export \{[^}]*\};?\s*$/m, '')
		.replace(/^export default MomijiUICanvas;\s*$/m, '');
	if (/^\s*(import|export)\b/m.test(body)) throw new Error('src/momiji-ui.mjs に import/export が残っている（1 ファイルの ES module だけを前提にしている）');
	return '// momiji-ui-canvas.js — **生成物。源は Tools/momiji-ui/web/src/momiji-ui.mjs**（node Tools/momiji-ui/web/build.mjs で作り直す。手で直さない）。\n'
		+ '// <script src="momiji-ui-canvas.js"> で読むと window.MomijiUICanvas が入る。ES module で使うなら momiji-ui.mjs を import する。\n'
		+ '(function (global) {\n\'use strict\';\n' + body.replace(/\n{3,}/g, '\n\n').trimEnd() + '\nglobal.MomijiUICanvas = MomijiUICanvas;\n})(typeof window !== \'undefined\' ? window : globalThis);\n';
}

export const CSS = `/* momiji-ui.css — 標準のスタイル。論理 1280×720 の板（stage）に canvas と文字欄の <input> を重ねる */
.momiji-ui-stage { position: absolute; left: 0; top: 0; width: 1280px; height: 720px; transform-origin: 0 0; overflow: hidden; }
.momiji-ui-stage canvas { position: absolute; left: 0; top: 0; width: 1280px; height: 720px; }
/* 文字欄: 枠は canvas が描くので、ここは文字だけ。日本語入力（IME）のために本物の <input> を重ねる */
.momiji-ui-stage input { position: absolute; box-sizing: border-box; margin: 0; padding: 0 12px; border: 0; outline: none;
  background: transparent; color: #e6edf6; font-family: "KaedeGothic", "BIZ UDPGothic", "Yu Gothic UI", "Yu Gothic", "Meiryo", sans-serif; caret-color: #5ab0ff; }
.momiji-ui-stage input::placeholder { color: #9aa6b8; }
.momiji-ui-stage input:disabled { color: #9aa6b8; }
/* 同梱書体（fonts/）。family 名は MomijiUI が使う "KaedeGothic"（MomijiUICanvas.FONT_FAMILY） */
${fontFaceCss()}
`;

const readme = (version) => `# MomijiUI（ブラウザ版）配布物

MomijiUI ${version}（UE のプラグインと同じ番号）。

画面を JSON（UiNode の木）で書き、UE とブラウザで同じ配置・同じ操作で動かす UI フレームワーク MomijiUI のブラウザ実装です。
この folder だけで動きます（UE も KaedeVN も要りません）。静的な HTTP 配信（\`npx serve\` など。file:// では書体と fetch が働きません）で \`examples/counter/\` を開いてください。

| ファイル | 何 |
|---|---|
| \`momiji-ui.mjs\` | ES module。\`import { MomijiUICanvas } from './momiji-ui.mjs'\` |
| \`momiji-ui-canvas.js\` | \`<script src>\` で読む従来形（\`window.MomijiUICanvas\`）。momiji-ui.mjs から機械的に作ったもの |
| \`momiji-ui.css\` | 標準のスタイル（stage・canvas・文字欄の <input>・同梱書体の @font-face） |
| \`fonts/\` | 同梱書体 BIZ UDP ゴシック（SIL OFL 1.1。\`fonts/OFL.txt\`） |
| \`examples/counter/\` | 最小の見本（画面 JSON \`counter.json\` は UE の見本と同じファイル） |

使い方（4 つの仕事）は \`examples/counter/main.js\` の順に: 画面 JSON を読む（\`MomijiUICanvas.load\`）→ 状態から値を作って描く（\`present\`）→ 押された action で状態を変える（\`attach\` の \`onEvent\`）→ 描き直す。
入力（ポインタ・ホイール・方向キーと Enter・文字欄の <input>・窓に合わせる \`fit\`）は \`attach\` が受け持ちます。終わるときは \`host.dispose()\`。

説明書は MomijiUI の導入ガイドとリファレンス（日本語と英語）です。UE のプラグインの \`Docs/\`（\`momiji-ui-guide.md\`・\`momiji-ui-reference.md\`）にあります。GitHub のリポジトリは準備中です。
`;

function build(check) {
	const esm = fs.readFileSync(SRC, 'utf8');
	const classic = classicFrom(esm);
	if (check) {
		const cur = fs.existsSync(CLASSIC) ? fs.readFileSync(CLASSIC, 'utf8') : '';
		if (cur !== classic) { console.log('FAIL: Scripts/lib/momiji-ui-canvas.js が源（Tools/momiji-ui/web/src/momiji-ui.mjs）から作ったものと違う。node Tools/momiji-ui/web/build.mjs で作り直す'); process.exit(1); }
		console.log('OK: Scripts/lib/momiji-ui-canvas.js は源と同じ');
		return;
	}
	const version = pluginVersion();
	const mark = (open, close) => `${open} MomijiUI ${version} — 版の正は UE の MomijiUI.uplugin の VersionName（node Tools/momiji-ui/web/build.mjs が書く）${close}\n`;
	fs.writeFileSync(CLASSIC, classic);
	fs.rmSync(DIST, { recursive: true, force: true });
	fs.mkdirSync(path.join(DIST, 'fonts'), { recursive: true });
	fs.mkdirSync(path.join(DIST, 'examples', 'counter'), { recursive: true });
	fs.writeFileSync(path.join(DIST, 'momiji-ui.mjs'), mark('//', '') + esm);
	fs.writeFileSync(path.join(DIST, 'momiji-ui-canvas.js'), mark('//', '') + classic);
	fs.writeFileSync(path.join(DIST, 'momiji-ui.css'), mark('/*', ' */') + CSS);
	fs.writeFileSync(path.join(DIST, 'README.md'), readme(version));
	for (const { file } of FONTS) fs.copyFileSync(path.join(FONT_DIR, file), path.join(DIST, 'fonts', file));
	const ofl = fs.readdirSync(FONT_DIR).find((f) => /^OFL/i.test(f));
	if (ofl) fs.copyFileSync(path.join(FONT_DIR, ofl), path.join(DIST, 'fonts', 'OFL.txt'));
	// 見本: index.html は配布物の置き方（momiji-ui.css と ../../momiji-ui.mjs）に合わせて書き換える
	const ex = path.join(TOOL_DIR, 'examples', 'counter');
	fs.writeFileSync(path.join(DIST, 'examples', 'counter', 'index.html'),
		fs.readFileSync(path.join(ex, 'index.html'), 'utf8').replace('href="../momiji-ui.css"', 'href="../../momiji-ui.css"'));
	fs.writeFileSync(path.join(DIST, 'examples', 'counter', 'main.js'),
		fs.readFileSync(path.join(ex, 'main.js'), 'utf8').replace("from '../momiji-ui.mjs'", "from '../../momiji-ui.mjs'").replace("fetch('../counter.json')", "fetch('counter.json')"));
	fs.copyFileSync(path.join(ROOT, 'Plugins', 'MomijiUI', 'Examples', 'counter.json'), path.join(DIST, 'examples', 'counter', 'counter.json'));
	console.log(`OK: MomijiUI ${version} ${path.relative(ROOT, DIST)}（momiji-ui.mjs・momiji-ui-canvas.js・momiji-ui.css・fonts・examples/counter）と ${path.relative(ROOT, CLASSIC)}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) build(process.argv.includes('--check'));
