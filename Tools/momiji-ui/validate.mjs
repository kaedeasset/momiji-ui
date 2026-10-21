#!/usr/bin/env node
// momiji-ui/validate.mjs — 画面 JSON を診断する（仕様 Docs/Guides/Working/momiji-ui-framework.md §12）。
//
//   node Tools/momiji-ui/validate.mjs <JSON ...>      # 指定したファイル
//   node Tools/momiji-ui/validate.mjs                 # Game/ui/**/*.json と Game/adv*/ui/*.json と RPG 制作ツールの既定の画面
//                                                  # （Plugins/KaedeRPG/Resources/Rpg/ui）を全部（照合のフィクスチャは除く）
//
// 1 件 1 行: <severity> <code> <ファイル><path>: <message>。error があれば終了コード 1（warn だけなら 0）。
// 規則は Tools/momiji-ui/web/src/momiji-ui.mjs の validate（UE の kaedevn::ui::validateScreen と同じ）。源の ES module を Node で直接 import する
import fs from 'node:fs';
import path from 'node:path';
import { validate } from './web/src/momiji-ui.mjs';
import { ROOT } from './lib/paths.mjs';

function listAll() {
	const out = [];
	const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (p.endsWith('.json')) out.push(p); } };
	walk(path.join(ROOT, 'Game', 'ui'));
	for (const w of fs.readdirSync(path.join(ROOT, 'Game')).filter((x) => x.startsWith('adv'))) {
		const d = path.join(ROOT, 'Game', w, 'ui');
		if (fs.existsSync(d)) for (const f of fs.readdirSync(d)) if (f.endsWith('.json')) out.push(path.join(d, f));
	}
	const rpg = path.join(ROOT, 'Plugins', 'KaedeRPG', 'Resources', 'Rpg', 'ui');
	if (fs.existsSync(rpg)) for (const f of fs.readdirSync(rpg)) if (f.endsWith('.json')) out.push(path.join(rpg, f));
	return out.filter((p) => !p.includes('momiji-ui-conformance'));
}

const files = process.argv.length > 2 ? process.argv.slice(2) : listAll();
let screens = 0, errors = 0, warns = 0;
for (const f of files) {
	let j;
	try { j = JSON.parse(fs.readFileSync(f, 'utf8')); } catch (e) { console.log(`error bad-json ${f}: ${e.message}`); errors++; continue; }
	if (!j || typeof j !== 'object' || (!j.root && !j.type)) continue;   // 画面でない JSON（manifest 等）
	screens++;
	for (const d of validate(j)) {
		if (d.severity === 'error') errors++; else warns++;
		console.log(`${d.severity} ${d.code} ${path.relative(ROOT, f)}${d.path}: ${d.message}`);
	}
}
console.log(`VALIDATE ${errors ? 'FAIL' : 'PASS'} screens=${screens} errors=${errors} warns=${warns}`);
process.exit(errors ? 1 : 0);
