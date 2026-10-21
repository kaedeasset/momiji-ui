// momiji-ui/lib/args.mjs — 道具の引数の読み方を 1 つに。"--名前 値" の対だけ（値の無い旗は持たない）。
//   const o = parseArgs(process.argv.slice(2), { defaults: { engine: 'chrome' }, required: (o) => ['chrome', 'port'] });
// 要る引数が無ければ "--<名前> が要る" を出して終了コード 2
export function parseArgs(argv, { defaults = {}, required = () => [] } = {}) {
	const o = Object.assign({}, defaults);
	for (let i = 0; i < argv.length; i += 2) o[argv[i].replace(/^--/, '')] = argv[i + 1];
	const missing = required(o).filter((k) => !o[k]);
	if (missing.length) { console.error(missing.map((k) => `--${k}`).join(' と ') + ' が要る'); process.exit(2); }
	return o;
}
