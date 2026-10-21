# MomijiUI の照合用フィクスチャ

機能ごとに 1 枚。UE（自動試験 `MomijiUI.MomijiUIConformance`）とブラウザ（ブラウザ版の照合の脚本）で同じ木を配置し、
押せる部品（action が `t:` で始まるもの）の矩形を突き合わせる。照合の脚本と自動試験のソースは MomijiUI の GitHub のリポジトリ（<https://github.com/kaedeasset/momiji-ui>）に入る（Fab の zip には入らない）。回し方はそのリポジトリの README。
`images/landscape.png` は Image の fit を見るための絵（作者が描いた図形だけ。MIT）。

- `root` … 画面の木（1280×720）
- `bind` … 任意。`strings`（`{key}` の文字列）・`vars`（条件式の数）・`lists`（repeat の一覧）
- `scenarios` … 任意。本物のキャンバスで通す操作の列 `[{name, steps: [...]}]`（UE は `SKaedeUiCanvas`、ブラウザは `attach` 越しに DevTools の本物の入力）。手は
  `{"move": [dx, dy]}`（方向キー）・`{"bind": {...}}`（値を変えて描き直す）・`{"scroll": {"id", "y"}}`（スクロール量）・`{"hit": [x, y]}`（押せる部品の鍵を記録）・
  `{"measure": {"node", "item"}}`（描かずに高さを測る）・`{"click": [x, y]}`（押して離す）・`{"probe": [x, y]}`（その点で入力を受けるのは欄 `input:<id>` かキャンバス `canvas` か）・
  `{"type": "文字"}`（打鍵）・`{"enter": true}`・`{"tab": true}`・`{"imeEnter": true}`（変換中の Enter。ブラウザだけ、UE は何もしない）・`{"screen": "名前"}`（画面の名前を変えて描き直す）・
  `{"reset": "名前"}`（その画面の状態を捨てる）・`{"input": {"id", "value"}}`（host が欄に値を入れる）・`{"select": [[x0, y0], [x1, y1]]}`（押す→動かす→離す。文字の選択）。
  各手のあとに 焦点の鍵・最前面の群・スクロール量・押した鍵・高さ・編集中の欄 `active`・画面に出ている欄の中身 `inputs`・押された数 `events`・編集器の見える矩形 `editor`・文字の選択 `sel`（`{group, from: [行, 字], to: [行, 字], text}` か null）を記録し、UE とブラウザで突き合わせる
- 手に `"expect": {...}` を書くと、UE とブラウザ**それぞれ**をその期待値と比べる（鍵は上の記録と同じ名前。`scroll` と `inputs` は書いた鍵だけ）。
  仕様から決めた合格条件はここに書く（両実装が同じ誤りを持っていても一致だけでは PASS にならない）
- 比べるもの: 当たり（`t:` の action・鍵と矩形と移動先）・描画命令の全部（矩形・枠・文字の行・画像・線・楕円の位置）・シナリオの各手
- 機能を足したら、その機能を使うフィクスチャを足す（片方の実装だけにある機能を作らない）
