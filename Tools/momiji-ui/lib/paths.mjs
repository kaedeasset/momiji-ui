// momiji-ui/lib/paths.mjs — MomijiUI のブラウザ側の道具が使う置き場（リポジトリの根・同梱書体・生成物）を 1 か所で決める。
// 使う側: web/build.mjs・conformance.mjs・examples/serve.mjs・examples/check.mjs・validate.mjs。ここの外で ROOT や書体の名前を計算しない
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '..', '..', '..');   // Tools/momiji-ui/lib → リポジトリの根（OSS の書き出しでも同じ並び）
export const TOOL_DIR = path.join(ROOT, 'Tools', 'momiji-ui');
export const SRC = path.join(TOOL_DIR, 'web', 'src', 'momiji-ui.mjs');   // ブラウザ版の源
export const CLASSIC = path.join(ROOT, 'Scripts', 'lib', 'momiji-ui-canvas.js');   // <script> で読む従来形（生成物）
export const SAVED = path.join(ROOT, 'Saved');

// 同梱書体 BIZ UDP ゴシック（SIL OFL 1.1）。family 名は MomijiUI が使う "KaedeGothic"（MomijiUICanvas.FONT_FAMILY）
export const FONT_DIR = path.join(ROOT, 'Plugins', 'MomijiUI', 'Resources', 'Fonts');
export const FONTS = [{ file: 'BIZUDPGothic-Regular.ttf', weight: 400 }, { file: 'BIZUDPGothic-Bold.ttf', weight: 700 }];
// @font-face の行（dir は CSS から見た書体の置き場。既定 "fonts/"）
export const fontFaceCss = (dir = 'fonts/') =>
	FONTS.map((f) => `@font-face { font-family: "KaedeGothic"; font-weight: ${f.weight}; src: url("${dir}${f.file}"); }`).join('\n');
// 静的サーバの道筋 "/fonts/<書体>" → ファイル
export const fontRoutes = () => Object.fromEntries(FONTS.map((f) => ['/fonts/' + f.file, path.join(FONT_DIR, f.file)]));
