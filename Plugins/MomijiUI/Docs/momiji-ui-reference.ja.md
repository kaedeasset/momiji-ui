# MomijiUI リファレンス

この説明書は MomijiUI 0.1.0（2026-10-21）のものです。

画面を JSON で書き、Unreal Engine（UE）とブラウザで同じ配置に描く UI フレームワーク MomijiUI の、画面 JSON の鍵・部品（17 種）とその鍵・列挙の値・動き・テーマ・診断のコードを全部引ける説明書です。表は、2 つの実装（UE とブラウザ）の診断の表から脚本が作っています。

- 見本を動かし、自分の画面を 1 つ書いて検査するまでの手順は[導入ガイド](momiji-ui-guide.ja.md)にあります。初めての人はそちらから読んでください。
- [English](momiji-ui-reference.md)

---

## 目次

1. [このリファレンスの読み方](#1-このリファレンスの読み方)
2. [画面 JSON](#2-画面-json)
3. [部品](#3-部品)
4. [配置](#4-配置)
5. [値の差し込み](#5-値の差し込み)
6. [焦点と当たり・選択](#6-焦点と当たり選択)
7. [動き](#7-動き)
8. [テーマ](#8-テーマ)
9. [操作（あなたのプログラムの仕事）](#9-操作あなたのプログラムの仕事)
10. [検査（診断）](#10-検査診断)
11. [保証すること・しないこと](#11-保証することしないこと)
12. [同梱物の権利](#12-同梱物の権利)

---

## 1. このリファレンスの読み方

### 1.1 単位と色

- **画面は 1280×720 として書きます。** 位置と大きさ（`x` `y` `w` `h` `pad` `gap` `margin` `wrapW` `lineH`）はこの上の論理 px です。実際の窓には縦横比のまま拡大して出し、余りは黒帯にします。
- **`fontSize` はポイントです。** 画面では `fontSize × 4/3` px（17 → 23px）。書かなければ 16。
- **色は `#rrggbb` か `#rrggbbaa`** です。それ以外の文字（`red` など）は無視されて既定の色になります（診断 `bad-color`）。
- 書体は同梱の BIZ UDP ゴシックで、OS の書体は使いません（§12）。

### 1.2 表の読み方

- **型**: 診断が見る値の型です。「（検査しない）」は、診断が知っている鍵として通すだけで形を見ない鍵です（形は「何を書くか」に書いています）。
- **既定**: 書かなかったときの値です。「—」は既定が無い（書かなければ働かない）ことです。
- **ブラウザ**: 「描く」は UE とブラウザの両方が描きます。「**UE だけ**」は UE だけが描き、ブラウザは診断で知っている鍵として通しますが描きません（見た目の描き方・枠など。§11）。
- 部品の表は共通の鍵（§3.1）を繰り返しません。全部の部品に §3.1 の鍵も書けます。
- 鍵の名前は大文字と小文字を区別します。`_` で始まる鍵は注記として扱い、診断も描画器も見ません。

### 1.3 2 つの実装

| | UE | ブラウザ |
|---|---|---|
| 描く道具 | Slate（`MomijiUISlate` の `SKaedeUiCanvas`） | HTML の `<canvas>`（`momiji-ui.mjs`） |
| 画面 JSON | 同じファイル | 同じファイル |
| 立場 | 参照実装 | UE に合わせる |

同じ画面 JSON と同じ値からは、2 つの実装で同じ配置・同じ押せる場所・同じ焦点の動きになります（§11）。

---

## 2. 画面 JSON

画面ファイルは `root` に部品の木を持つオブジェクトです。`root` が無いファイルは、ファイル全体を 1 つの部品として読みます。

```json
{
 "root": {
  "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 200, "pad": 20, "gap": 12,
  "children": [
   { "type": "Label", "text": "Hello {name}", "h": 32, "fontSize": 20, "align": "center" },
   { "type": "Button", "id": "ok", "text": "OK", "action": "ok", "h": 44 }
  ]
 }
}
```

最上位に書ける鍵:

<!-- momiji-ui-ref:begin doc-keys -->
| 鍵 | 何を書くか |
|---|---|
| `id` | 画面の名前（任意）。描画器は使わない。照合の試料では試料の名前 |
| `note` | 人が読む注記（任意）。描画器も診断も中身を見ない |
| `title` | 画面の題（任意）。描画器は使わない |
| `root` | 画面の部品の木の根（オブジェクト）。これがあると最上位の鍵も診断する。無ければファイル全体を 1 つの部品として読む |
| `bind` | 描くときの値 {strings, vars, lists}（任意）。UE の起動引数 -KaedeUiShot と照合が、この値で描く。ゲームでは host が値を渡すので要らない |
| `scenarios` | 照合の操作の筋書き（任意・照合の試料だけ）。ゲームの画面では書かない |
| `invalid` | true なら照合が診断だけを比べる（わざと誤りを入れた試料の印）。ゲームの画面では書かない |
| `host` | 画面を使う側（host）が読む値の置き場（任意のオブジェクト）。描画器と診断は中身を見ない |
<!-- momiji-ui-ref:end doc-keys -->

画面 JSON に版の番号はありません。今の正しい JSON は、これからの版でも同じ寸法で動きます。

---

## 3. 部品

部品は `type` で決まり、`children` で入れ子にします。並べる入れ物（VerticalLayout・HorizontalLayout・GridLayout・ScrollView・Modal）は子の位置を決めます。Panel は子を並べません。

![部品 17 種を 1 枚に並べた画面（UE。テーマ midnight）。Polygon と Divider はいまは UE だけが描く](images/momiji-ui/components.png)

### 3.1 共通の鍵

<!-- momiji-ui-ref:begin common-keys -->
全部の部品に書ける鍵（25）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `type` | 文字列 | Panel | 描く | 部品の種類（§3 の 17 種）。書かなければ Panel |
| `id` | 文字列 | — | 描く | 部品の名前。焦点・スクロール量・動き・文字欄の中身はこの名前で追う。状態を持つ部品（押せる・文字欄・ScrollView・Modal）には付ける。一覧の中は "row:{item.id}" のようにデータの鍵で |
| `x` | 数 | 0 | 描く | 左端（論理 px）。並べる入れ物の子では親が決める |
| `y` | 数 | 0 | 描く | 上端（論理 px）。並べる入れ物の子では親が決める |
| `w` | 数 | 0 | 描く | 幅（論理 px）。縦に並べる入れ物の子で書かなければ親の幅いっぱい |
| `h` | 数 | 0 | 描く | 高さ（論理 px）。書かなければ中身から測る（並べる入れ物・折り返す Label）か 28 |
| `margin` | 数 | 0 | 描く | 並べる入れ物の中での外側の余白（上下左右に同じだけ） |
| `flex` | 数 | 0 | 描く | 縦／横に並べる入れ物の子で、大きさを書いていない分の残りを分ける比（1 と 2 なら 1:2） |
| `minW` | 数 | 0 | 描く | flex で分けたときの幅の下限（横に並べる入れ物の子） |
| `minH` | 数 | 0 | 描く | flex で分けたときの高さの下限（縦に並べる入れ物の子） |
| `anchor` | 文字列 | — | 描く | 画面の端や中央に寄せる（値は §4.1）。描く前に一度だけ解く |
| `showIf` | 文字列 | — | 描く | 条件式（§5.2）。偽なら子ごと消す |
| `hideIf` | 文字列 | — | 描く | 条件式（§5.2）。真なら子ごと消す |
| `enabledIf` | 文字列 | — | 描く | 条件式（§5.2）。偽なら押せない（薄く描く） |
| `highlightIf` | 文字列 | — | 描く | 条件式（§5.2）。真なら強調（highlight と同じ） |
| `enabled` | 真偽 | true | 描く | false なら押せない（薄く描く） |
| `focusable` | 真偽 | true | 描く | false なら方向キー・パッドの焦点に入らない（押すことはできる） |
| `highlight` | 真偽 | false | 描く | true なら強調（板と入れ物は accent の枠と明るい地、枠だけのボタンは accent の枠） |
| `action` | 文字列 | — | 描く | 押したときに host へ届く文字列。意味は host が決める。Button のほか Panel・Image・並べる入れ物・Slider・Toggle・ToggleGroup の項目に書けば押せる。TextInput は Enter で届く |
| `anim` | オブジェクト | — | 描く | 動き（§7）。動く部品には id か action を付ける |
| `offset` | （検査しない） | — | UE だけ | [x, y]。並べた位置からずらす（はみ出してよい）。UE だけ |
| `rotate` | 数 | 0 | UE だけ | 度。部品の中心を軸に中身ごと回す。UE だけ |
| `skewX` | 数 | 0 | UE だけ | 度。部品の中心を軸に中身ごと横へ斜めにする。UE だけ |
| `opacity` | 数 | 1 | UE だけ | 0〜1。中身ごと薄くする。UE だけ |
| `glow` | （検査しない） | — | UE だけ | {size, color}。後ろに柔らかい光の輪を描く。UE だけ |
<!-- momiji-ui-ref:end common-keys -->

### 3.2 Panel

<!-- momiji-ui-ref:begin type-Panel -->
板。**子を並べない**（子の x・y・w・h は画面の上の位置のまま使う。積むなら VerticalLayout）。押せる板にするなら action を書く。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `variant` | 文字列 | テーマの panel の板 | 描く | 見た目: spacer（何も描かない）・light・dark・atelier-surface・atelier-card・atelier-icon-tile |
| `color` | 文字列 | テーマの panel | 描く | 地の色（#rrggbb か #rrggbbaa） |
| `borderColor` | 文字列 | テーマの panelEdge | UE だけ | 枠の色。UE だけ |
| `borderWidth` | 数 | テーマの border | UE だけ | 枠の太さ（px）。0 は枠なし（テーマの二重線や飾りも描かない）。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | {"dir": "v" か "h", "stops": [["#rrggbbaa", 0], ["#…", 1]]}。地を線形の濃淡で塗る（color より強い）。UE だけ |
| `shape` | 文字列 | rect | UE だけ | 箱の形: rect・hex（両端が尖る）・tagL・tagR（片方が尖る）・slant（平行四辺形）。UE だけ |
| `shapeInset` | 数 | 高さの 0.35 | UE だけ | 尖りの深さ（px）。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。立体の縁（上と左が light、下と右が dark）。UE だけ |
| `innerBorder` | （検査しない） | — | UE だけ | {color, width, inset}。内側のもう 1 本の線。UE だけ |
| `clip` | 真偽 | false | UE だけ | true なら中身を箱で切る。UE だけ |
| `backdropBlur` | 数 | 0 | UE だけ | 強さ（2〜20）。後ろの画面をぼかす。UE だけ |
| `highlightTextColor` | 文字列 | — | UE だけ | 強調している間（highlight）の中の文字の色。UE だけ |
| `selectable` | 真偽 | false | 描く | true なら中の Label の文字をマウスで選べ、離した瞬間にクリップボードへ入る（§6.2） |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1）。host が渡す一覧の要素ごとに型を複製して子にする |
| `template` | オブジェクト | — | 描く | repeat の型（部品 1 つ） |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型}。要素の templateKey の値で型を選ぶ |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "Panel", "x": 40, "y": 40, "w": 400, "h": 160, "variant": "dark",
  "children": [
   {"type": "Label", "x": 60, "y": 60, "w": 360, "h": 32, "text": "Score {score}"}
  ] } }
```
<!-- momiji-ui-ref:end type-Panel -->

### 3.3 Image

<!-- momiji-ui-ref:begin type-Image -->
画像か色の四角。sprite も color も無ければテーマの panel の色。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `sprite` | 文字列 | — | 描く | 画像の場所（鍵）。場所への解決は host（UE はプロジェクトからの相対、ブラウザは imageSource） |
| `fit` | 文字列 | contain | 描く | 画像の収め方（値は §4.1） |
| `color` | 文字列 | テーマの panel | 描く | 画像が無いときの塗りの色 |
| `borderColor` | 文字列 | — | UE だけ | 枠の色。書いたときだけ枠を描く（角は直角・画像の上）。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 枠の太さ（px）。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | 色の四角を線形の濃淡で塗る（形は Panel と同じ。sprite が無く color も書いたときだけ効く）。UE だけ |
| `slice` | （検査しない） | — | UE だけ | [左, 上, 右, 下]（絵の px）。9 分割で伸ばす。UE だけ |
| `tile` | 真偽 | false | UE だけ | true なら敷き詰める。UE だけ |
| `src` | （検査しない） | — | UE だけ | [x, y, w, h]（絵の px）。絵の一部を切り出す。UE だけ |

例:

```json
{ "root": {"type": "Image", "x": 40, "y": 40, "w": 320, "h": 180, "sprite": "Plugins/MomijiUI/Fixtures/images/landscape.png", "fit": "cover"} }
```
<!-- momiji-ui-ref:end type-Image -->

### 3.4 Label

<!-- momiji-ui-ref:begin type-Label -->
文字。1 行の文字は箱の縦の中心に置く。wrapW を書くと折り返し、高さは行数から決まる。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `text` | 文字列 | — | 描く | 文字。{鍵} は値に置き換わる（§5）。\n で改行 |
| `fontSize` | 数 | 16 | 描く | 字の大きさ（ポイント。画面では ×4/3 px） |
| `font` | 文字列 | bold | 描く | 太さ: bold（太字）・main（標準）。logo はブラウザは太字・UE は標準 |
| `align` | 文字列 | left | 描く | 横の寄せ（値は §4.1） |
| `center` | 真偽 | false | 描く | true なら中央寄せ（古い書き方。align があればそちら） |
| `wrapW` | 数 | 0 | 描く | 折り返す幅（px）。書くと複数行で、左上から行送りで下へ |
| `lineH` | 数 | fontSize × 30/22（無ければ 30） | 描く | 行送り（px。折り返すときだけ） |
| `ellipsis` | 真偽 | false | 描く | true なら入らない文字を末尾で落として「…」を付ける（1 行） |
| `textColor` | 文字列 | テーマの textPrimary（押せないときは textDim） | 描く | 文字の色 |
| `runs` | 配列 | — | 描く | [{text, bold, color, ruby}]。区間ごとに色・太字・ルビを変える（text の代わりに描く）。UE はさらに size（区間の大きさ）と icon・src（行の中の絵） |
| `textOutline` | （検査しない） | — | UE だけ | {width, color}。文字の縁取り。UE だけ |
| `textGlow` | （検査しない） | — | UE だけ | {size, color}。文字の光。UE だけ |
| `letterSpacing` | 数 | テーマ | UE だけ | 字間（px）。UE だけ |
| `monoDigits` | 真偽 | false | UE だけ | true なら数字を同じ幅で並べる。UE だけ |
| `highlightTextColor` | 文字列 | — | UE だけ | 強調している入れ物の中にいる間の文字の色。UE だけ |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 480, "h": 160, "pad": 16, "gap": 8,
  "children": [
   {"type": "Label", "text": "Chapter {chapter}", "fontSize": 20, "align": "center", "h": 32},
   {"type": "Label", "text": "A long line of text that wraps inside the box.", "font": "main", "wrapW": 440, "textColor": "#9aa6b8"}
  ] } }
```
<!-- momiji-ui-ref:end type-Label -->

### 3.5 Button

<!-- momiji-ui-ref:begin type-Button -->
押せる板と文字。押すと action が host へ届く。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `text` | 文字列 | — | 描く | ボタンの文字（{鍵} は値に置き換わる） |
| `variant` | 文字列 | primary | 描く | 見た目: primary（accent で塗る）・ghost（panel で塗って枠）・atelier-primary・atelier-ghost。UE はさらに plain（文字だけ）・box（窓と同じ描き方） |
| `icon` | 文字列 | — | 描く | 文字の前に付ける記号（絵文字など） |
| `align` | 文字列 | center | 描く | ghost のときだけ left・right で左右 10px 内側に寄せる（ほかは中央） |
| `font` | 文字列 | bold | 描く | 太さ: bold（太字）・main（標準）。logo はブラウザは太字・UE は標準 |
| `fontSize` | 数 | 16 | 描く | 字の大きさ（ポイント） |
| `ellipsis` | 真偽 | false | 描く | true なら入らない文字を「…」で詰める |
| `color` | 文字列 | primary はテーマの accent（押せないと accentDim）・ghost は panel | 描く | 地の色 |
| `textColor` | 文字列 | primary は textOnAccent・ghost は textPrimary | 描く | 文字の色 |
| `shape` | 文字列 | rect | UE だけ | 箱の形（Panel と同じ値）。UE だけ |
| `shapeInset` | 数 | — | UE だけ | 尖りの深さ（px）。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。立体の縁。UE だけ |
| `textOutline` | （検査しない） | — | UE だけ | {width, color}。文字の縁取り。UE だけ |
| `textGlow` | （検査しない） | — | UE だけ | {size, color}。文字の光。UE だけ |
| `letterSpacing` | 数 | — | UE だけ | 字間（px）。UE だけ |

例:

```json
{ "root": { "type": "HorizontalLayout", "anchor": "bottom", "h": 64, "pad": 8, "gap": 8,
  "children": [
   {"type": "Button", "id": "ok", "text": "OK", "action": "ok", "w": 160},
   {"type": "Button", "id": "cancel", "text": "Cancel", "action": "cancel", "variant": "ghost", "w": 160, "enabledIf": "canCancel"}
  ] } }
```
<!-- momiji-ui-ref:end type-Button -->

### 3.6 Slider

<!-- momiji-ui-ref:begin type-Slider -->
溝と、値の分だけ塗った棒。押した位置の比（0〜1）が値として host へ届く（kind: slider）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `bindVar` | 文字列 | — | 描く | 値の名前。host の数の値（vars）が value に入る |
| `value` | 数 | 0 | 描く | 0〜1 の値 |
| `color` | 文字列 | テーマの accent | 描く | 塗った棒の色 |
| `text` | 文字列 | — | UE だけ | 0〜1 の数の文字（"{hp}" など）。書けば value の代わりに使う。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | 塗った棒を線形の濃淡で塗る。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。溝の立体の縁。UE だけ |
| `shape` | 文字列 | — | UE だけ | 溝と棒の形（Panel と同じ値）。UE だけ |
| `shapeInset` | 数 | — | UE だけ | 尖りの深さ（px）。UE だけ |
| `borderColor` | 文字列 | — | UE だけ | 溝の枠の色。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 溝の枠の太さ（px）。UE だけ |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 80, "pad": 16, "gap": 8,
  "children": [
   {"type": "Label", "text": "Volume", "h": 24},
   {"type": "Slider", "id": "volume", "bindVar": "volume", "action": "set:volume", "h": 14}
  ] } }
```
<!-- momiji-ui-ref:end type-Slider -->

### 3.7 Toggle

<!-- momiji-ui-ref:begin type-Toggle -->
四角の印と右の文字。押すと切り替えたあとの真偽が host へ届く（kind: toggle）。当たりは箱＋10＋文字の幅。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `bindVar` | 文字列 | — | 描く | 値の名前。host の数の値（vars）が 0 以外なら on |
| `on` | 真偽 | false | 描く | 入っているか |
| `text` | 文字列 | — | 描く | 印の右の文字 |
| `font` | 文字列 | main | 描く | 文字の太さ |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 60, "pad": 16,
  "children": [
   {"type": "Toggle", "id": "subtitles", "bindVar": "subtitles", "action": "toggle:subtitles", "text": "Subtitles"}
  ] } }
```
<!-- momiji-ui-ref:end type-Toggle -->

### 3.8 ToggleGroup

<!-- momiji-ui-ref:begin type-ToggleGroup -->
タブのような排他の選択。子（各項目の text と action）を横に等分し、選んだ項目の上に accent の帯。押すと項目の番号が host へ届く（kind: toggle）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `bindVar` | 文字列 | — | 描く | 値の名前。host の数の値（vars）が selected に入る |
| `selected` | 数 | 0 | 描く | 選んでいる項目の番号（0 から） |
| `children` | 配列 | — | 描く | 項目の配列（各項目に text と action） |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 480, "h": 64, "pad": 8,
  "children": [
   { "type": "ToggleGroup", "id": "speed", "bindVar": "speed", "h": 40,
    "children": [
     {"id": "speed-0", "text": "Slow", "action": "speed:0"},
     {"id": "speed-1", "text": "Normal", "action": "speed:1"},
     {"id": "speed-2", "text": "Fast", "action": "speed:2"}
    ] }
  ] } }
```
<!-- momiji-ui-ref:end type-ToggleGroup -->

### 3.9 ScrollView

<!-- momiji-ui-ref:begin type-ScrollView -->
子を縦に積み、はみ出したら縦にスクロールする（flex は効かない）。中身が箱より高いと子の幅から 10 を引いてスクロール条を出す。見える範囲の外は描かず押せない。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `pad` | 数 | 0 | 描く | 内側の余白 |
| `gap` | 数 | 8 | 描く | 子の間 |
| `variant` | 文字列 | — | 描く | light か dark で地を塗る |
| `color` | 文字列 | — | 描く | 地の色（書けば塗る） |
| `selectable` | 真偽 | false | 描く | true なら中の Label の文字を選んでコピーできる（§6.2） |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1） |
| `template` | オブジェクト | — | 描く | repeat の型 |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型} |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "ScrollView", "id": "log", "x": 40, "y": 40, "w": 600, "h": 300, "pad": 8, "gap": 4, "selectable": true, "repeat": "lines", "children": [],
  "template": {"type": "Label", "text": "{item.text}", "font": "main", "wrapW": 560} } }
```
<!-- momiji-ui-ref:end type-ScrollView -->

### 3.10 Modal

<!-- momiji-ui-ref:begin type-Modal -->
画面全体の暗幕と中央の板。板は w×h（既定 480×320）で、x・y が 0 なら画面の中央。子は縦に積む。Modal の中は別の焦点の群で、開いている間は後ろの部品に焦点も入力も行かない。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `text` | 文字列 | — | 描く | 見出し（板の上端から 4 下に高さ 36）。あれば子は 48 下から |
| `dim` | 真偽か数 | true | 描く | false なら暗幕を描かない |
| `pad` | 数 | 0 | 描く | 板の内側の余白 |
| `gap` | 数 | 8 | 描く | 子の間 |
| `color` | 文字列 | テーマの panel | 描く | 板の色 |
| `borderColor` | 文字列 | — | UE だけ | 板の枠の色。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 板の枠の太さ（px）。UE だけ |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1） |
| `template` | オブジェクト | — | 描く | repeat の型 |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型} |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "Panel", "variant": "spacer",
  "children": [
   { "type": "Modal", "id": "confirm", "text": "Quit?", "showIf": "askQuit", "pad": 16, "gap": 8,
    "children": [
     {"type": "Button", "id": "quit-yes", "text": "Yes", "action": "quit:yes", "h": 44},
     {"type": "Button", "id": "quit-no", "text": "No", "action": "quit:no", "variant": "ghost", "h": 44}
    ] }
  ] } }
```
<!-- momiji-ui-ref:end type-Modal -->

### 3.11 VerticalLayout

<!-- momiji-ui-ref:begin type-VerticalLayout -->
子を上から縦に積む。幅の無い子は親の幅いっぱい。高さの無い入れ物の子は測った高さを持つ。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `pad` | 数 | 0 | 描く | 内側の余白 |
| `gap` | 数 | 8 | 描く | 子の間 |
| `variant` | 文字列 | — | 描く | light か dark で地を塗る |
| `color` | 文字列 | — | 描く | 地の色（書けば塗る） |
| `borderColor` | 文字列 | — | UE だけ | 枠の色。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 枠の太さ（px）。0 は枠なし。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | 地を線形の濃淡で塗る。UE だけ |
| `shape` | 文字列 | — | UE だけ | 地の形（Panel と同じ値）。UE だけ |
| `shapeInset` | 数 | — | UE だけ | 尖りの深さ（px）。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。立体の縁。UE だけ |
| `innerBorder` | （検査しない） | — | UE だけ | {color, width, inset}。内側の線。UE だけ |
| `clip` | 真偽 | — | UE だけ | true なら中身を箱で切る。UE だけ |
| `backdropBlur` | 数 | — | UE だけ | 後ろの画面をぼかす強さ。UE だけ |
| `highlightTextColor` | 文字列 | — | UE だけ | 強調している間の中の文字の色。UE だけ |
| `selectable` | 真偽 | false | 描く | true なら中の Label の文字を選んでコピーできる（§6.2） |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1） |
| `template` | オブジェクト | — | 描く | repeat の型 |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型} |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 240, "pad": 16, "gap": 8, "variant": "dark",
  "children": [
   {"type": "Label", "text": "Title", "h": 32},
   {"type": "Panel", "variant": "light", "flex": 1},
   {"type": "Button", "id": "close", "text": "Close", "action": "close", "h": 44}
  ] } }
```
<!-- momiji-ui-ref:end type-VerticalLayout -->

### 3.12 HorizontalLayout

<!-- momiji-ui-ref:begin type-HorizontalLayout -->
子を左から横に並べる。子の幅は w（無ければ 100）か flex。高さの無い子は親の高さいっぱい。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `pad` | 数 | 0 | 描く | 内側の余白 |
| `gap` | 数 | 8 | 描く | 子の間 |
| `variant` | 文字列 | — | 描く | light か dark で地を塗る |
| `color` | 文字列 | — | 描く | 地の色（書けば塗る） |
| `borderColor` | 文字列 | — | UE だけ | 枠の色。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 枠の太さ（px）。0 は枠なし。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | 地を線形の濃淡で塗る。UE だけ |
| `shape` | 文字列 | — | UE だけ | 地の形（Panel と同じ値）。UE だけ |
| `shapeInset` | 数 | — | UE だけ | 尖りの深さ（px）。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。立体の縁。UE だけ |
| `innerBorder` | （検査しない） | — | UE だけ | {color, width, inset}。内側の線。UE だけ |
| `clip` | 真偽 | — | UE だけ | true なら中身を箱で切る。UE だけ |
| `backdropBlur` | 数 | — | UE だけ | 後ろの画面をぼかす強さ。UE だけ |
| `highlightTextColor` | 文字列 | — | UE だけ | 強調している間の中の文字の色。UE だけ |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1） |
| `template` | オブジェクト | — | 描く | repeat の型 |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型} |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "HorizontalLayout", "anchor": "top", "h": 56, "pad": 8, "gap": 8,
  "children": [
   {"type": "Label", "text": "HP {hp}", "w": 160},
   {"type": "Panel", "variant": "spacer", "flex": 1},
   {"type": "Button", "id": "menu", "text": "Menu", "action": "menu", "w": 120}
  ] } }
```
<!-- momiji-ui-ref:end type-HorizontalLayout -->

### 3.13 GridLayout

<!-- momiji-ui-ref:begin type-GridLayout -->
子を cols 列に左上から並べる。セル幅は等分、行はその行の最大の高さ＋gap だけ下げる。高さの無い子は 28。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `cols` | 数 | 1 | 描く | 列の数 |
| `pad` | 数 | 0 | 描く | 内側の余白 |
| `gap` | 数 | 8 | 描く | セルの間（縦と横） |
| `variant` | 文字列 | — | 描く | light か dark で地を塗る |
| `color` | 文字列 | — | 描く | 地の色（書けば塗る） |
| `borderColor` | 文字列 | — | UE だけ | 枠の色。UE だけ |
| `borderWidth` | 数 | — | UE だけ | 枠の太さ（px）。0 は枠なし。UE だけ |
| `gradient` | （検査しない） | — | UE だけ | 地を線形の濃淡で塗る。UE だけ |
| `shape` | 文字列 | — | UE だけ | 地の形（Panel と同じ値）。UE だけ |
| `shapeInset` | 数 | — | UE だけ | 尖りの深さ（px）。UE だけ |
| `edge3d` | （検査しない） | — | UE だけ | {light, dark, width}。立体の縁。UE だけ |
| `innerBorder` | （検査しない） | — | UE だけ | {color, width, inset}。内側の線。UE だけ |
| `clip` | 真偽 | — | UE だけ | true なら中身を箱で切る。UE だけ |
| `backdropBlur` | 数 | — | UE だけ | 後ろの画面をぼかす強さ。UE だけ |
| `highlightTextColor` | 文字列 | — | UE だけ | 強調している間の中の文字の色。UE だけ |
| `children` | 配列 | — | 描く | 子の部品の配列 |
| `repeat` | 文字列 | — | 描く | 一覧の名前（§5.1） |
| `template` | オブジェクト | — | 描く | repeat の型 |
| `templates` | オブジェクト | — | 描く | {"<種類>": 型} |
| `templateKey` | 文字列 | — | 描く | templates から型を選ぶ要素の鍵 |

例:

```json
{ "root": { "type": "GridLayout", "anchor": "center", "w": 480, "h": 200, "cols": 3, "pad": 8, "gap": 8, "repeat": "items", "children": [],
  "template": {"type": "Button", "id": "item:{item.id}", "text": "{item.name}", "action": "use:{item.id}", "h": 44} } }
```
<!-- momiji-ui-ref:end type-GridLayout -->

### 3.14 TextInput

<!-- momiji-ui-ref:begin type-TextInput -->
1 行の文字欄の枠。編集は MomijiUI が枠の位置に重ねる編集欄（UE は SEditableText、ブラウザは <input>）で、日本語入力が使える。Enter で action が届く（kind: submit・value は欄の文字）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `bindVar` | 文字列 | — | 描く | 欄の中身の名前（host はこの名前で読み書きする） |
| `placeholder` | 文字列 | — | 描く | 空のときに薄く出す例文 |
| `maxLen` | 数 | 0 | 描く | 最大の字数（コードポイント。0 は無制限） |
| `focused` | 真偽 | false | 描く | true なら枠を accent で描く（host が付ける） |
| `color` | 文字列 | テーマの track | 描く | 欄の地の色 |
| `fontSize` | 数 | 16 | 描く | 字の大きさ（ポイント） |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 480, "h": 80, "pad": 16,
  "children": [
   {"type": "TextInput", "id": "name", "bindVar": "name", "placeholder": "Your name", "maxLen": 12, "action": "name:submit", "h": 44}
  ] } }
```
<!-- momiji-ui-ref:end type-TextInput -->

### 3.15 Polyline

<!-- momiji-ui-ref:begin type-Polyline -->
折れ線。配置に参加しない（points は画面の上の位置）。2 点未満なら描かない。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `points` | 配列 | — | 描く | [[x, y], …]（画面の上の位置） |
| `thickness` | 数 | 1 | 描く | 線の太さ |
| `closed` | 真偽 | false | 描く | true なら終点と始点も結ぶ（枠線になる） |
| `color` | 文字列 | UE はテーマの textPrimary・ブラウザは panelEdge | 描く | 線の色 |

例:

```json
{ "root": { "type": "Panel", "variant": "spacer",
  "children": [
   {"type": "Polyline", "points": [[100, 400], [300, 300], [500, 360]], "thickness": 3, "color": "#5ab0ff"}
  ] } }
```
<!-- momiji-ui-ref:end type-Polyline -->

### 3.16 Ellipse

<!-- momiji-ui-ref:begin type-Ellipse -->
楕円。x・y・w・h が外接する四角。配置に参加しない。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `fill` | 真偽 | true | 描く | true なら塗る。false なら輪郭だけ |
| `thickness` | 数 | 1 | 描く | 輪郭の太さ（fill が false のとき） |
| `color` | 文字列 | テーマの textPrimary | 描く | 色 |

例:

```json
{ "root": { "type": "Panel", "variant": "spacer",
  "children": [
   {"type": "Ellipse", "x": 600, "y": 300, "w": 80, "h": 80, "fill": false, "thickness": 2, "color": "#e0a04a"}
  ] } }
```
<!-- momiji-ui-ref:end type-Ellipse -->

### 3.17 Polygon

<!-- momiji-ui-ref:begin type-Polygon -->
塗りの多角形。points は部品の左上からの位置で、並べる入れ物の中に置ける（w・h を持つ）。3 点未満なら描かない。UE だけ。

ブラウザはこの部品の形を描かず、箱をテーマの panel の色で塗る（診断は知っている部品として通す）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `points` | 配列 | — | UE だけ | [[x, y], …]（部品の左上から） |
| `color` | 文字列 | テーマの panel | UE だけ | 塗りの色 |
| `gradient` | （検査しない） | — | UE だけ | 線形の濃淡で塗る |
| `borderColor` | 文字列 | — | UE だけ | 輪郭の色（書けば輪郭を描く） |
| `borderWidth` | 数 | — | UE だけ | 輪郭の太さ（px） |

例:

```json
{ "root": { "type": "HorizontalLayout", "anchor": "top", "h": 48, "pad": 8,
  "children": [
   {"type": "Polygon", "w": 32, "h": 32, "points": [[0, 0], [32, 16], [0, 32]], "color": "#e0a04a"}
  ] } }
```
<!-- momiji-ui-ref:end type-Polygon -->

### 3.18 Divider

<!-- momiji-ui-ref:begin type-Divider -->
区切り線。箱の縦の中央に線を引き、ornament で飾りを付ける。高さが無ければ 12。UE だけ。

ブラウザはこの部品の形を描かず、箱をテーマの panel の色で塗る（診断は知っている部品として通す）。

| 鍵 | 型 | 既定 | ブラウザ | 何を書くか |
|---|---|---|---|---|
| `color` | 文字列 | テーマの panelEdge | UE だけ | 線と飾りの色 |
| `thickness` | 数 | 1 | UE だけ | 線の太さ（1 より細くしない） |
| `ornament` | 文字列 | none | UE だけ | 飾り: diamond（中央に菱形 3 つ）・dots（両端と中央に小さな菱形）・none |

例:

```json
{ "root": { "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 80, "pad": 16,
  "children": [
   {"type": "Divider", "ornament": "diamond", "thickness": 1}
  ] } }
```
<!-- momiji-ui-ref:end type-Divider -->

---

## 4. 配置

### 4.1 列挙の値

診断が値を確かめる鍵です。表の外の値は `bad-value`（error）になります。

<!-- momiji-ui-ref:begin enums -->
| 鍵 | 値 | 何になるか |
|---|---|---|
| `anchor` | `fill` | 画面全体 |
| `anchor` | `safe` | 安全域全体 |
| `anchor` | `top` | 安全域の幅いっぱいで上へ寄せる |
| `anchor` | `bottom` | 安全域の幅いっぱいで下へ寄せる |
| `anchor` | `left` | 高さいっぱいで左へ寄せる |
| `anchor` | `right` | 高さいっぱいで右へ寄せる |
| `anchor` | `center` | 中央（w・h はそのまま） |
| `fit` | `contain` | 画像を枠の中に収める（余白ができる） |
| `fit` | `cover` | 枠を埋める（はみ出しは切る） |
| `fit` | `stretch` | 枠に合わせて伸ばす（縦横比は変わる） |
| `align` | `left` | 左寄せ |
| `align` | `center` | 中央寄せ（字面の左右の中心で） |
| `align` | `right` | 右寄せ |
| `anim.enter.type` | `fade` | 透明から不透明へ（既定 easeOutQuad） |
| `anim.enter.type` | `slide` | from（既定 bottom）から distance（既定 40）だけ滑り込む（既定 easeOutCubic） |
| `anim.enter.type` | `pop` | 0.85 倍から元の大きさへ（既定 easeOutCubic） |
<!-- momiji-ui-ref:end enums -->

### 4.2 高さの決め方

`h` が 0 より大きければそれを使います。無ければ種類ごとに測ります。

| 部品 | 高さ |
|---|---|
| VerticalLayout | `2·pad ＋ Σ(2·margin ＋ 子の高さ) ＋ gap·(子の数−1)` |
| HorizontalLayout | `2·pad ＋ max(2·margin ＋ 子の高さ)` |
| GridLayout | `2·pad ＋ 行数·行の高さ ＋ gap·(行数−1)`（行の高さはその行の子の最大） |
| `wrapW` のある Label | 折り返した行数 × 行送り |
| それ以外 | 28 |

### 4.3 並べる入れ物

- **VerticalLayout・HorizontalLayout**: 主軸の残り＝内寸 − Σ(`flex` の無い子の大きさ ＋ 2·margin) − gap·子の数。`flex` の子は残りを `flex` の比で分け、`minH`・`minW` を下回りません。
- **GridLayout**: セル幅＝`floor((w − 2·pad − (cols−1)·gap) / cols)`。幅の無い子はセル幅 − 2·margin。
- **ScrollView**: 縦に積みます（`flex` は効きません）。スクロール量は `id` ごとに持ち、`0〜(中身−箱)` に収めます。スクロール条は幅 6・右端から 2 内側。
- **Modal**: 子は板の中で縦に積みます。同じ親に Modal が 2 つ同時に出たときは、後に描いた（手前の）Modal だけが入力を受けます。
- **Panel**: 子を並べません。Panel の中の子は自分の `x` `y` `w` `h` のまま描かれます（大きさが 0 なら見えません）。
- **Polyline・Ellipse** は配置に参加しません（画面の上の位置で描く）。**Polygon** は `w` `h` を持つので並べる入れ物の中に置けます。

### 4.4 anchor

`anchor` は描く前に一度だけ解きます。値は §4.1 の表のとおりです。`anchor` の無い部品は `x` `y` `w` `h` を画面の上の位置として使います。

---

## 5. 値の差し込み

あなたのプログラム（host）は、描くたびに 3 種類の値を渡します。画面 JSON の元の木は書き換えず、写しに入れます。

| 種類 | 何に効くか |
|---|---|
| 文字（strings） | `text` `sprite` `action` の中の `{鍵}` |
| 数（vars） | 条件式と `{鍵}`（文字に無ければ数を使う）。`bindVar` の部品（Slider の value・Toggle の on・ToggleGroup の selected） |
| 一覧（lists） | `repeat` で並べる部品 |

知らない `{鍵}` はそのまま残ります（診断もしません）。

### 5.1 repeat と template

- `repeat: "<一覧の名前>"` の部品は、一覧の要素ごとに `template` を複製して子にします。
- `templates: {"<種類>": 型}` と `templateKey: "<要素の鍵>"` があれば、要素の `item[templateKey]` で型を選びます（無ければ `template`、それも無ければその要素は出しません）。
- 複製の `text` `action` `id` `sprite` `enabledIf` `highlightIf` `showIf` `hideIf` の `{item.<鍵>}` を要素の値で置き換えます。入れ子の repeat も同じです。
- 一覧の中の部品の `id` は `"row:{item.id}"` のように、データの安定した鍵で付けます。

### 5.2 条件式

`showIf` `hideIf` `enabledIf` `highlightIf` と `anim.pulse.whileIf` に書く式です。

- 比較は `名前 演算子 数`（`==` `!=` `>=` `<=` `>` `<`）。数だけ（0 以外が真）・名前だけ（値が 0 以外で真）も書けます。
- `&&`（かつ）・`||`（または）・`!`（でない）・括弧が使えます。
- 名前は英字か `_` で始まり、英数字・`_`・`.` が続きます（`item.potion` など）。知らない名前は 0 です。
- 名前は数（vars）で渡します。文字（strings）は条件に使えません。

例: `count > 0`・`count >= 5 && ready`・`!(done) || n == 2`。

### 5.3 描くまでの順

1. 数の値を `bindVar` の部品へ入れる
2. `repeat` を展開する
3. `{鍵}` を置き換える
4. 条件を当てる（`enabledIf` が偽なら押せない・`highlightIf` が真なら強調・`showIf` が偽／`hideIf` が真の部品は子ごと消す）
5. 配置して描き、押せる場所を作る

1〜5 を 1 回でする入口が UE の `KaedeUI::PresentScreen`、ブラウザの `MomijiUICanvas.present` です（§9）。

---

## 6. 焦点と当たり・選択

### 6.1 当たりと焦点

- **当たり**（押せる場所）は、`action` を持ち押せる部品の四角です。後に描いたものが勝ちます。押せない（`enabled` が偽）部品は当たりになりません。ScrollView の見える範囲の外は押せません。
- **部品の鍵**: `id` があれば `id:<id>`、無ければ `action:<action>`。焦点・スクロール量・動き・文字欄の中身はこの鍵で追います。同じ `action` の部品が 2 つあってよいので、状態を持つ部品には `id` を付けます。
- **焦点の移動**（方向キー・ゲームパッド）: 同じ群（Modal の外が 0、Modal はそれぞれ別の群）の中で、その方向にある当たりのうち、距離 ＋ 横（縦）のずれ × 2 が最小のものへ移ります。移った先が ScrollView の見える範囲の外なら、見えるところまでスクロールします。
- **Modal を開くと**焦点は Modal の中の先頭へ移り、閉じると前の部品へ戻ります。Modal が開いている間、後ろの部品にはキーも押下も届きません。
- **焦点の枠**は方向キーを使っている間だけ、当たりの 3 外側に太さ 3 の橙色で出ます。ポインタを動かすと消えます。
- Enter・Space は、いちばん手前の群の焦点の部品を押します。

### 6.2 文字の選択とコピー

- `selectable: true` を書いた `Panel`・`VerticalLayout`・`ScrollView` の中の **Label の文字**（runs の本文を含む）をマウスでなぞると選ばれ、**離した瞬間にクリップボードへ入ります**（「コピーしました」の札が出ます。Ctrl+C は要りません）。
- 選べるのは器 1 つの中だけです。ボタン・文字欄・ルビの文字は選べません。指（タッチ）では選べません（中身のスクロールになります）。
- 同じ Label の折り返しは続けて、Label が変わるところは改行して写します。
- 器の外・押せる部品・文字欄を押したとき、画面が変わったとき、選んでいた行の文字が変わったときに選択は消えます。

---

## 7. 動き

`anim` に書きます。**動く部品には `id` か `action` を付けます**（動きはその鍵で追うので、無いと動きません）。入れ子の `anim` は外側が勝ちます。画面（host が渡す画面の名前）が変わったら、画面全体が 160ms で浮かび、前の画面の動きは捨てます。

<!-- momiji-ui-ref:begin anim-keys -->
| 鍵 | ブラウザ | 何を書くか |
|---|---|---|
| `enter` | 描く | {type, from, distance, durationMs, delayMs, easing}。初めて描かれたときに出る動き（type は §4.1）。既定 220ms |
| `countUp` | 描く | {durationMs, easing}。文字が数（1,234 も可）で、前に描いた値から変わったとき数え上げる（初回は即時）。既定 400ms |
| `pulse` | 描く | {scale, periodMs, whileIf}。箱を中心から拡大縮小し続ける（文字は動かない）。whileIf が偽なら止まる。既定 1.06 倍・900ms |
| `staggerMs` | 描く | 同じ親の中の順番 × これだけ enter を遅らせる（一覧を順に出す） |
| `blink` | UE だけ | {periodMs, min}。不透明度を min〜1 で繰り返す。UE だけ |
| `bob` | UE だけ | {distance, periodMs}。上下に揺らす。UE だけ |
| `frames` | UE だけ | {count, periodMs}。Image の src の切り出しを横に 1 コマずつ進める。UE だけ |
| `exit` | UE だけ | {type: fade か close, durationMs}。消えるとき、しばらく描き続けて消す。UE だけ |
| `tween` | UE だけ | {durationMs}。Slider の棒の長さを前の値から補間する。UE だけ |
<!-- momiji-ui-ref:end anim-keys -->

- `enter` の `type` に書ける値は §4.1 の 3 つです。UE の描画器は `"open"`（縦に中心から伸びる）も描きますが、診断（UE・ブラウザとも）は `"open"` を `bad-value`（error）として出し、ブラウザの `load` はその画面を止めます。
- `pulse` で拡大縮小するのは箱だけで、文字の大きさと位置は変わりません。

---

## 8. テーマ

テーマは画面全体の色と形の組です。UE は `KaedeCore::BuiltinTheme("<名前>")` か `KaedeCore::LoadTheme(<テーマ JSON の文字列>)`、ブラウザは `R.setTheme("<名前>")` か `R.setTheme({…})` で選びます。テーマの JSON には変えたい鍵だけを書きます（UE の `LoadTheme` は atelier を元に、ブラウザの `R.setTheme({…})` はいまのテーマを元に、書いた鍵だけを替えます）。

| atelier | midnight | sakura |
|---|---|---|
| ![内蔵テーマ atelier の設定の画面（UE）](images/momiji-ui/theme-atelier.png) | ![内蔵テーマ midnight の設定の画面（UE）](images/momiji-ui/theme-midnight.png) | ![内蔵テーマ sakura の設定の画面（UE）](images/momiji-ui/theme-sakura.png) |

<!-- momiji-ui-ref:begin theme-keys -->
内蔵のテーマ 3 つ（`atelier`・`midnight`・`sakura`）。テーマの JSON の鍵は 29 で、ブラウザが読むのはそのうち色の 11。

| 鍵 | ブラウザ | 何を変えるか |
|---|---|---|
| `bg` | 描く | 画面の地の色 |
| `panel` | 描く | 板・窓・ghost のボタン・Modal の板の色 |
| `panelLight` | 描く | variant: light の板の色 |
| `panelEdge` | 描く | 枠と線の色 |
| `accent` | 描く | 強調の色（primary のボタン・Slider の棒・強調の枠） |
| `accentDim` | 描く | 押せない primary のボタンの色 |
| `track` | 描く | Slider の溝・文字欄の地・variant: dark の地 |
| `dim` | 描く | Modal の暗幕の色 |
| `textPrimary` | 描く | 文字の色 |
| `textDim` | 描く | 薄い文字（押せない部品・例文）の色 |
| `textOnAccent` | 描く | accent の上の文字の色 |
| `radius` | UE だけ | 窓とボタンの角の丸み（px）。UE だけ |
| `border` | UE だけ | 枠の太さ（px・既定 1）。UE だけ |
| `frame` | UE だけ | 窓の飾り: double（内側にもう 1 本）・ornate（さらに四隅に飾り）。UE だけ |
| `shadow` | UE だけ | {x, y, blur, color}。窓の影。UE だけ |
| `gradient` | UE だけ | 窓の地の濃淡（色 1 つなら下へ重ねる色、{dir, stops} なら線形の濃淡）。UE だけ |
| `bevel` | UE だけ | 窓の内側の上端の明るい線の色。UE だけ |
| `textShadow` | UE だけ | {x, y, color}。文字の影。UE だけ |
| `cursorBar` | UE だけ | 選んでいる行の左の accent の帯の幅（0 は無し）。UE だけ |
| `edge3d` | UE だけ | {light, dark, width}。窓の立体の縁。UE だけ |
| `innerBorder` | UE だけ | {color, width, inset}。窓の内側の線。UE だけ |
| `frameImage` | UE だけ | {sprite, slice}。窓の枠を絵の 9 分割で描く。UE だけ |
| `pattern` | UE だけ | {sprite, opacity}。窓の地に敷き詰める模様。UE だけ |
| `windowShape` | UE だけ | 窓の形（部品の shape と同じ値）。UE だけ |
| `select` | UE だけ | {gradient, fill, border, text, cursor, glow}。選んでいる行の見た目。UE だけ |
| `textOutline` | UE だけ | 全部の文字の縁取りの既定。UE だけ |
| `textGlow` | UE だけ | 全部の文字の光の既定。UE だけ |
| `letterSpacing` | UE だけ | 全部の文字の字間の既定（px）。UE だけ |
| `knockout` | UE だけ | true なら半透明の窓どうしが重なった所を濃くしない。UE だけ |
<!-- momiji-ui-ref:end theme-keys -->

---

## 9. 操作（あなたのプログラムの仕事）

あなたのプログラムの仕事は 4 つです: **画面 JSON を読む → 状態から値を作って描く → 押された動作で状態を変える → 描き直す。** 入力（ポインタ・ホイール・スクロール条のつまみ・指のスクロール・Slider のドラッグ・方向キーと Enter/Space・文字欄の編集・日本語入力）は MomijiUI が受け持ち、あなたのプログラムに届くのは事象だけです。座標や当たりを自分で解釈しないでください。

### 9.1 事象

事象は `{kind, action, nodeId, key, value, relX}` です。

| kind | いつ | value |
|---|---|---|
| `press` | ボタンなど押せる部品を押した | 無し |
| `submit` | 文字欄で Enter（日本語の変換中の Enter は届かない） | 欄の文字列 |
| `slider` | Slider を押した・動かした | 0〜1 の数 |
| `toggle` | Toggle・ToggleGroup を押した | Toggle は切り替えたあとの真偽、ToggleGroup は押した項目の番号 |

### 9.2 UE

| すること | 呼ぶもの |
|---|---|
| 読む | `KaedeCore::LoadScreenTree(<JSON の文字列>)`（読めなければエラーをログに出して空の木）。解析済みの JSON からは `ScreenTreeFromJson` |
| 描く | `KaedeUI::PresentScreen(Canvas, Tree, Values, Theme, Bg, Motion, ScreenId)`（§5.3 の 1〜5 を 1 回で）。値は `KaedeUI::FKaedeScreenValues`（`Strings`・`Vars`・`Lists`） |
| 押されたとき | `SKaedeUiCanvas::SetOnAction([](const FString& Action, float RelX){ … })` |
| 文字欄 | `GetTextInputValue("<bindVar>")`・`SetTextInputValue`・`SetActiveTextInput` |
| 画面ごとの状態 | `ResetScreenState(<画面の名前>)`・`ResetAllScreenState`・`SetScrollOffset` |
| 動き | `kaedevn::ui::UiMotionState` をウィジェットで持ち、`PresentScreen` に渡し、Tick で `update(<ミリ秒>)` |
| 書体 | `SKaedeUiCanvas::SetFontFamily`（Gothic・Mincho・Hand・Dot。§12） |
| 描かずに測る | `KaedeUI::MeasureNodeHeight`・`KaedeCore::InstantiateTemplate` |
| 検査 | `KaedeCore::ValidateScreenJson`（§10） |
| 画面だけを見る | 起動引数 `-KaedeUiShot=<画面 JSON>`（1 枚撮って終わる。`bind` の値で描く） |

モジュールの `*.Build.cs` で `MomijiUICore` と `MomijiUISlate` に依存させます。見本は `Plugins/MomijiUI/Source/MomijiUISlate/Private/Examples/SMomijiUICounterExample.cpp` です。

### 9.3 ブラウザ

| すること | 呼ぶもの |
|---|---|
| 読む | `MomijiUICanvas.load(json)`（診断して error があれば例外で止まる。`{strict: false}` で続ける。戻り値 `{root, host, diagnostics}`） |
| 書体を待つ | `await MomijiUICanvas.loadFonts()` |
| 描く | `MomijiUICanvas.present(R, tree, {strings, vars, lists}, bg)`（`R` は `new MomijiUICanvas.Renderer(canvas)`） |
| 入力を受ける | `MomijiUICanvas.attach(R, canvas, {stage, render, onEvent, drafts, fit})` |
| 文字欄 | `host.inputValue`・`host.setInputValue`・`host.focusInput`（`host` は `attach` の戻り値） |
| 画面ごとの状態 | `R.screenId`・`R.resetScreen`・`R.resetScreens` |
| 画像 | `R.imageSource`・`R.onImageLoad`・`R.onImageError` |
| 終わる | `host.dispose()`（入力だけやめるなら `host.detach()`） |
| 描かずに測る・配置だけ | `R.measureNode`・`R.layout(tree, bg)`・`R.paint()`・`R.hitAt` |
| 検査 | `MomijiUICanvas.validate(doc)`（§10） |

ブラウザ版は依存の無い ES module 1 本（`momiji-ui.mjs`）と、`<script>` で読む従来形（`window.MomijiUICanvas`）です。静的な HTTP のサーバーで開きます（`file://` では書体と `fetch` が働きません）。ブラウザの配布物は GitHub で準備中です。

---

## 10. 検査（診断）

診断は描く前に、場所（JSON Pointer の path）つきで次を返します。描くときには呼びません（検査の道具です）。UE は `KaedeCore::ValidateScreenJson`、ブラウザは `MomijiUICanvas.validate(doc)`、手元のファイルは `node Tools/momiji-ui/validate.mjs <画面 JSON …>`（1 件 1 行・error があれば終了コード 1）。2 つの実装は同じコードを同じ path で出します。

![誤りを 5 つ入れた画面 JSON（左）と、その診断の結果（右。重さ・コード・path・説明）。ブラウザの検査の結果を画面に描いたもの](images/momiji-ui/diagnostics.png)

<!-- momiji-ui-ref:begin diagnostics -->
| コード | 重さ | 何 | 直し方 |
|---|---|---|---|
| `bad-json` | error | ファイルが JSON として読めない（node Tools/momiji-ui/validate.mjs だけが出す） | 出た位置の JSON の書き方（括弧・カンマ・引用符）を直す |
| `bad-value` | error | 値の型違い（数・文字列・真偽・配列・オブジェクト）、列挙の外の値（anchor・fit・align・anim.enter.type）、runs・points の要素の形 | §3 の表の型と §4.1 の値に合わせる |
| `dup-id` | error / warn | 同じ id（{ を含まないもの）が 2 つある。状態を持つ部品（押せる・文字欄・ScrollView・Modal）が絡めば error、見た目だけの部品どうしは warn | id を別にする。一覧の中は "row:{item.id}" のようにデータの鍵で |
| `template-missing` | error | repeat に template も templates も無い。templateKey に templates が無い | template か templates を足す |
| `unknown-type` | error | 知らない type（描けない） | §3 の 17 種のどれかに直す（綴りと大文字小文字） |
| `bad-color` | warn | #rrggbb・#rrggbbaa でない色（無視されて既定の色になる）。$ で始まる値は見ない | #rrggbb か #rrggbbaa で書く |
| `selectable-ignored` | warn | selectable を Panel・VerticalLayout・ScrollView 以外に書いた（無視される） | 文字を入れた器（Panel・VerticalLayout・ScrollView）に書く |
| `unknown-key` | warn | 知らない鍵（無視される。綴り違いの発見用）。_ で始まる鍵は注記として見ない | §3 の表の鍵の綴りに直す。注記なら _ で始める |
<!-- momiji-ui-ref:end diagnostics -->

- 重さ: **error** は描けないか意図どおりにならないもの、**warn** は無視されるが描けるもの。
- 未知の `{鍵}` は診断しません（補間の決まりどおり残ります）。

---

## 11. 保証すること・しないこと

| 保証する | 保証しない |
|---|---|
| 同じ木・同じ値・同じ字の計測からは同じ配置（四角の位置と大きさは 1px 以内・文字の行の位置は 2px 以内）・折り返しの行数・repeat と条件の結果・押せる場所・焦点の鍵と移り先と Modal の開閉・スクロール量・描かずに測った高さ・診断の結果 | 画素の色（字の塗り・角や縁のなめらかさ・画像の補間・色の混ぜ方）。描く道具ごとに違います |

- 保証は照合の試料（`Plugins/MomijiUI/Fixtures/` の 19 枚）で、UE と Windows の Chrome（ほかに Chromium・Firefox・WebKit）を突き合わせて確かめています。
- **「UE だけ」の鍵と部品**（見た目の描き方・枠・Polygon・Divider・テーマの形の鍵・`blink` などの動き）は、ブラウザではその見た目で描かれません（Polygon・Divider は板の色の四角になります）。配置の保証は、それらを書かない画面のものです。
- 同梱の書体を読み終える前に描いた結果は保証の外です（ブラウザは `loadFonts` を待ってから描く）。製品版の Safari（macOS）・Edge・Mac／Linux は確かめていません。
- 対応する UE は 5.8・Windows です。

---

## 12. 同梱物の権利

| もの | 権利 | 場所 |
|---|---|---|
| MomijiUI（ソースと照合の試料・見本） | MIT License（kaedeasset） | `Plugins/MomijiUI/LICENSE` |
| BIZ UDP ゴシック（Regular・Bold）・BIZ UDP 明朝（Regular・Bold）（モリサワ） | SIL Open Font License 1.1 | `Resources/Fonts/`・許諾文 `OFL-BIZ-UDGothic.txt`・`OFL-BIZ-UDMincho.txt` |
| Yomogi（手書き風）・DotGothic16（ドット） | SIL Open Font License 1.1 | `Resources/Fonts/`・許諾文 `OFL-Yomogi.txt`・`OFL-DotGothic16.txt` |
| JSON for Modern C++（nlohmann/json 3.11.3） | MIT License | `Source/MomijiUICore/ThirdParty/nlohmann/` |

- 既定の書体は BIZ UDP ゴシックです。UE は `SKaedeUiCanvas::SetFontFamily` で明朝・手書き風・ドットに替えられます（Yomogi と DotGothic16 に太字はありません）。ブラウザはゴシックだけです。
- 書体を配るときは許諾文も一緒に配ります（UE は `MomijiUISlate.Build.cs` が配信物へ積みます）。
