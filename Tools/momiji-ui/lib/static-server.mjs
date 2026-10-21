// momiji-ui/lib/static-server.mjs — 見本と照合が使う小さな静的サーバ（依存なし）。
//   serveStatic(port, { routes, resolve, lan, notFound }) → Promise<http.Server>
//     routes   … { "/url": ファイルの道筋 | { type, body: () => 文字列か Buffer } }（先に見る）
//     resolve  … (url) => ファイルの道筋 | null（routes に無い url。".." を含む url はここへ来ない）
//     lan      … true なら 0.0.0.0 で待つ（既定は 127.0.0.1 だけ）
//     notFound … 404 の本文
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';

export const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
	'.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8', '.ttf': 'font/ttf', '.png': 'image/png' };

const isFile = (f) => !!f && fs.existsSync(f) && fs.statSync(f).isFile();

export function serveStatic(port, { routes = {}, resolve = null, lan = false, notFound = 'not found' } = {}) {
	const srv = http.createServer((req, res) => {
		let url;
		try { url = decodeURIComponent(req.url.split('?')[0]); } catch { res.writeHead(400); res.end(); return; }
		if (url.endsWith('/')) url += 'index.html';
		const r = routes[url];
		if (r && typeof r === 'object') { res.writeHead(200, { 'content-type': r.type }); res.end(r.body()); return; }
		const file = r || (resolve && !url.includes('..') ? resolve(url) : null);
		if (!isFile(file)) { res.writeHead(404); res.end(notFound); return; }
		res.writeHead(200, { 'content-type': TYPES[path.extname(file)] || 'application/octet-stream' });
		res.end(fs.readFileSync(file));
	});
	return new Promise((ok) => srv.listen(port, lan ? '0.0.0.0' : '127.0.0.1', () => ok(srv)));
}
