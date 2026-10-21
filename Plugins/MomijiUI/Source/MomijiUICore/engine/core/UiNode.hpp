// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// UiNode — MomijiUI の宣言的(データ駆動) UI「モデル」層。描画非依存。
//
// このヘッダは「画面を何で記述するか(UiNode ツリー / JSON)」だけを定義し、
// 「どう描くか」「どこから読むか(ファイル I/O)」は含まない。
// 描画器を差し替えてもこのモデルは不変で、同じ JSON が同じ配置の UI を生む「契約」になる。
// **仕様の正は Docs/Guides/Working/momiji-ui-framework.md**（2026-10-03）。実装は UE（このモデル＋
// MomijiUISlate の KaedeUiPainter / SKaedeUiCanvas・参照実装）とブラウザ（Tools/momiji-ui/web/src/momiji-ui.mjs）で、
// 照合は Scripts/momiji-ui-conformance.ps1。
//
//   [ JSON / UiNode ] --(host: Interp で補間/間引き)--> UiNode
//                     --(描画器: Slate / ブラウザの canvas)--> 描画
//
// 命名は Unity(uGUI) 準拠: Image / Label(Text) / Button / Slider / Toggle /
//   ToggleGroup / ScrollView / Modal / 自動レイアウト(Horizontal/Vertical/Grid)。
//
// 依存: 標準ライブラリと nlohmann::json の前方宣言（json_fwd.hpp）だけ。json.hpp は解析する .cpp が include する
//       （このヘッダを読む側に 2.4 万行と例外を強いない）。色は自前の Color を使う。

#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Export.hpp"   // MOMIJIUICORE_API

namespace kaedevn::ui {

// ---- 色 ----
// 本文色・テーマ色を含む全部の色。描画器が描く直前に自分の色の型へ直す。レイアウトは {r,g,b,a}。
struct Color { uint8_t r, g, b, a; };

// ---- テーマ(Skin) ----
// ---- 見た目の部品（2026-10-12・UE 先行。Docs/Guides-rpg/Working/rpg-themes/README.md §3）----
// 線形の濃淡。stops は位置 0〜1 の昇順。dir: 縦（上→下）か横（左→右）
struct UiGradient {
    bool on = false;
    bool horizontal = false;
    std::vector<std::pair<Color, float>> stops;
};
// 立体の縁（上と左が light、下と右が dark）
struct UiEdge3d { bool on = false; Color light{255, 255, 255, 255}; Color dark{0, 0, 0, 255}; int width = 2; };
// 内側のもう 1 本の線
struct UiInnerBorder { bool on = false; Color color{0, 0, 0, 255}; int width = 1; int inset = 4; };
// 文字の縁取り・光
struct UiTextOutline { bool on = false; int width = 2; Color color{0, 0, 0, 255}; };
struct UiTextGlow { bool on = false; int size = 6; Color color{0, 0, 0, 160}; };
// 後ろの柔らかい光の輪
struct UiGlow { bool on = false; int size = 16; Color color{255, 255, 255, 96}; };
// 絵（9 分割・敷き詰め）。sprite はテーマの JSON では相対、host が絶対パスに直す
struct UiSpriteRef { std::string sprite; int slice[4] = {0, 0, 0, 0}; bool over = false; float opacity = 1.f; int w = 0, h = 0, dx = 0, dy = 0; };
// 選んでいる行（highlight の入れ物）の見た目
struct UiSelectStyle {
    bool on = false;
    UiGradient gradient;
    bool hasFill = false; Color fill{0, 0, 0, 0};
    bool hasBorder = false; Color border{0, 0, 0, 0};
    bool hasText = false; Color text{0, 0, 0, 0};
    UiSpriteRef cursor;      // sprite が空なら無し
    UiGlow glow;
};

struct UiTheme {
    Color bg{0x1b, 0x14, 0x10, 255};
    Color panel{0x2a, 0x21, 0x1a, 255};
    Color panelLight{0x3a, 0x2e, 0x23, 255};
    Color panelEdge{0x4a, 0x3a, 0x2b, 255};
    Color accent{0xe0, 0xa0, 0x4a, 255};
    Color accentDim{0x8a, 0x6a, 0x38, 255};
    Color track{0x14, 0x0f, 0x0c, 230};
    Color dim{0, 0, 0, 160};        // モーダル背景の暗幕
    Color textPrimary{0xf3, 0xe9, 0xd8, 0xff};
    Color textDim{0xb8, 0xa9, 0x8f, 0xff};
    Color textOnAccent{0x1b, 0x14, 0x10, 0xff};
    // ---- 形と装飾（2026-10-12・UE 先行。ブラウザは未対応＝知らない鍵として無視）----
    int radius = -1;        // 窓・押しボタンの角の丸み（論理 px）。-1 は部品ごとの既定（窓 8・ボタン 10・Modal 16）
    int border = 1;         // 枠の太さ（論理 px）
    std::string frame;      // 窓の飾り: "" 一重 / "double" 内側にもう 1 本（枠の色）/ "ornate" さらに四隅に飾り（accent）
    // ---- 影・地の濃淡・文字の影・選択の印（2026-10-12・UE 先行。色の a が 0 なら描かない＝既定は今までと同じ絵）----
    Color shadowColor{0, 0, 0, 0}; int shadowX = 0, shadowY = 0, shadowBlur = 0;   // 窓の落とす影（blur は広がりの px）
    Color gradient{0, 0, 0, 0};    // 窓の地の下へ向かって重ねる色（上は 0・下でこの a）
    Color bevel{0, 0, 0, 0};       // 窓の内側の上端の明るい線（光の当たる縁）
    Color textShadow{0, 0, 0, 0}; int textShadowX = 1, textShadowY = 1;          // 文字の影
    int cursorBar = 0;             // 選んでいる行の左に置く accent の帯の幅（0 は無し）
    // ---- 7 テーマの描き方（2026-10-12・rpg-themes/README.md §3.2）----
    UiGradient windowGradient;     // "gradient" をオブジェクトで書いたとき（窓の地の濃淡）
    UiEdge3d edge3d;
    UiInnerBorder innerBorder;
    UiSpriteRef frameImage;        // 窓の枠の絵（9 分割）
    UiSpriteRef pattern;           // 窓の地の模様（敷き詰め・opacity）
    std::string windowShape;       // 窓の形（shape と同じ値）
    UiSelectStyle select;
    UiTextOutline textOutline;
    UiTextGlow textGlow;
    float letterSpacing = 0.f;     // 全部の文字の字間（px）
    bool knockout = false;         // 半透明の窓どうしが重なった所を濃くしない
};

// ---- ウィジェット種別(Unity 命名) ----
enum class UiType {
    Panel,            // 背景パネル(Image プリセット)
    Image,            // 矩形/枠/スプライト
    Label,            // Text (TMP 相当)
    Button,           // Button
    Slider,           // Slider / ProgressBar (value 0..1)
    Toggle,           // Toggle (on/off)
    ToggleGroup,      // タブ/排他選択 (selected で1つだけ active)
    ScrollView,       // 縦スクロールリスト (はみ出しはカリング)
    Modal,            // 暗幕 + 中央パネル + 入力ロック
    VerticalLayout,   // VerticalLayoutGroup
    HorizontalLayout, // HorizontalLayoutGroup
    GridLayout,       // GridLayoutGroup
    TextInput,        // 1行テキスト入力(名前入力欄 F2)。text=現在のバッファ、placeholder=空時表示
    // ---- 図形(F-2) ----
    // 「位置と線が意味を持つ図」(攻略地図・依存グラフ・タイムライン)を JSON だけで描くための最小形。
    // 箱と文字しか無いと、情報を並べることはできても図にはならない、というのが 2026-08-12 の結論。
    Polyline,         // 折れ線。points の頂点を順に結ぶ。closed=true で枠線にもなる
    Ellipse,          // 楕円。x/y/w/h が外接矩形。fill=false なら輪郭のみ
    // ---- 2026-10-12（UE 先行・rpg-themes/README.md §3.1）----
    Polygon,          // 塗りの多角形。points は部品の左上からの px。並べる入れ物の中に置ける
    Divider,          // 区切り線（中央・両端に飾り）
};

// Polyline の頂点(絶対座標・論理px)。レイアウトグループの子にはしない前提。
struct UiPoint { int x = 0, y = 0; };

// ---- 本文リッチテキスト(F1): Label の text を「装飾付き run の列」で表す ----
// 1 run = 連続した同一スタイルの文字列 + 任意のルビ。色/太字/ルビを区間ごとに変えられる。
// 供給元(Op/コンパイラの styled runs)は別働で、UI は描画モデルだけを定義する
// (本文はしばらく単一 run = フラット text のまま。runs があればそちらを優先描画)。
struct TextRun {
    std::string text;
    bool bold = false;            // 太字(font "bold" で描画)
    bool hasColor = false;
    Color color{0, 0, 0, 0xff};   // 指定時は run 個別色(未指定は Label の textColor)
    std::string ruby;             // ルビ(furigana)。空=ルビ無し。base の上に小サイズで中央寄せ
    int size = 0;                 // 2026-10-12: この区間の文字の大きさ(pt)。0=Label の大きさ。行の下を揃える
    std::string icon;             // 2026-10-12: 行の中の絵（場所）。text の代わりに描く
    int iconSrc[4] = {0, 0, 0, 0};// 絵の切り出し（w>0 のとき）
};

// layoutRuns の結果: 折り返し・ルビ確保込みで描画位置が確定した 1 片(atom)。
struct PlacedRun {
    std::string text;       // この片の本文(ルビ無し run は 1 文字ずつ、ルビ run は丸ごと)
    int x = 0, y = 0;       // 本文ベース行の左上(ルビ余白の下)
    bool bold = false;
    bool hasColor = false;
    Color color{0, 0, 0, 0xff};
    std::string ruby;       // この片に付くルビ(空=無し)。base 中央上に描く
    int width = 0;          // 本文片の幅(ルビ中央寄せに使う)
};

// runs を [x, x+wrapW] に流し込み、折り返し位置を確定する(純関数・テスト可能)。
// measure(text, bold) は本文フォントでの幅(px)を返すコールバック。lineH は 1 行の送り、
// rubyHeadroom はルビを置くための行上余白(ルビ run が無ければ 0 を渡してよい)。
// ルビ run は分割せず 1 片に保つ(furigana が割れないように)。非ルビ run は CJK 折り返しの
// ため 1 コードポイントずつに割る。web の同モデル実装と意味を合わせる前提。
std::vector<PlacedRun> layoutRuns(const std::vector<TextRun>& runs, int x, int y,
                                  int wrapW, int lineH, int rubyHeadroom,
                                  const std::function<int(const std::string&, bool bold)>& measure);

// ---- 宣言的ノード ----
// 1 つの可変フィールド集合で全ウィジェットを表す(AI が穴埋めしやすい)。
struct UiNode {
    UiType type = UiType::Panel;
    std::string id;        // 任意の識別子(ScrollView のスクロール状態キー等)
    // 元の JSON の場所（JSON Pointer。2026-10-16・UI エディタ U3。Docs/Guides-rpg/Working/rpg-ui-editor-design.md §5.2）。
    // 読み込み（nodeFromJson / LoadScreenTree）が付ける: {root: …} の包みなら "/root/children/1"、包みが無ければ "" と "/children/1"。
    // template の中は "<repeat の部品>/template/…"・templates は "…/templates/<種類>/…"。コードで作った部品は空。
    // fromRepeat: expandRepeats が template から複製した部品（の根）で true。srcPath は template の場所のまま
    std::string srcPath;
    bool fromRepeat = false;

    // レイアウト。レイアウトグループの子では w/h(必要なら)だけ指定し x/y は親が計算。
    int x = 0, y = 0, w = 0, h = 0;
    int gap = 8;       // レイアウトグループの子間隔
    int pad = 0;       // レイアウトグループの内側余白
    int margin = 0;    // レイアウト内での外側余白(上下左右)
    int cols = 1;      // GridLayout の列数

    // ---- LayoutElement 相当のサイズ指定(Phase D) ----
    // flex>0 の子は Horizontal/Vertical レイアウトの主軸方向で余り空間を flex 比で分配される
    // (Unity の flexibleWidth/Height 相当)。flex==0 は従来どおり w/h を preferred として使う。
    float flex = 0.0f;
    int minW = 0;      // flex 分配時の最小幅(下限)
    int minH = 0;      // flex 分配時の最小高さ(下限)

    // ---- アンカー(Phase D: セーフエリア/縦横レスポンシブ) ----
    // 空=従来どおり x/y/w/h を絶対座標として使う。指定時はビューポート/セーフエリアに対して
    // 解決される: "fill"(全画面ブリード) / "safe"(セーフエリアいっぱい) /
    // "top"/"bottom"/"left"/"right"(セーフエリアの各辺に寄せ、主軸はいっぱい) / "center"。
    std::string anchor;

    // ---- 条件表示(showIf/hideIf) ----
    // 空=常に表示。指定時は host の変数環境(VarEnv)で評価し、不可視ノードは
    // 描画前に prune される。
    //   showIf: 条件が偽なら隠す / hideIf: 条件が真なら隠す
    //   式: "name"(非0で真) | "name OP 数値"(== != > < >= <=)
    // 評価/間引きは ui::isVisible / ui::prune(Interp.hpp)。UiContext は関与しない。
    std::string showIf;
    std::string hideIf;

    // ---- 条件 highlight(highlightIf。選択状態の統一表現 S2・設計 07/21/02) ----
    // 空=highlight フィールドの静的値のまま。指定時は showIf/enabledIf と同じ式文法で評価し、
    // 真なら highlight=true(accent 枠+背景明化=「選択中/表示中」マーキング)。repeat 内は
    // "{item.<key>}" 補間→数値リテラル評価。評価は ui::applyEnabled(Interp.hpp)が兼務。
    std::string highlightIf;

    // ---- 条件 enabled(enabledIf) ----
    // 空=enabled フィールドの静的値のまま。指定時は showIf と同じ式文法で評価し、
    // 偽なら enabled=false(押せない+disabled 描画)。repeat テンプレート内では
    // "{item.<key>}" が要素スコープで補間されてから評価される(数値リテラルも可)。
    // 評価は ui::applyEnabled(Interp.hpp)。
    std::string enabledIf;

    // 共通プロパティ
    std::string text;      // Label / Button のテキスト
    std::string action;    // クリック時に発火する action 文字列
    std::string variant;   // Button: "primary" | "ghost"
    bool enabled = true;
    bool focusable = true; // キーボード/ゲームパッドのフォーカス対象にするか
    bool center = false;   // Label 中央寄せ(後方互換。align があればそちら優先)
    bool highlight = false;// Panel ハイライト枠
    // 文字の選択（2026-10-07・設計 Docs/Reports/2026/10/07/01）。Panel / VerticalLayout / ScrollView に書くと、
    // その器の中に描かれた Label の文字をマウスのドラッグで選べ、離した瞬間にクリップボードへ入る（選択は器 1 つの中だけ）。
    // 入れ子なら内側の器が 1 つの範囲。ほかの部品に書くと診断 selectable-ignored（warn）
    bool selectable = false;

    // 色オーバーライド(任意)。指定が無ければ theme を使う。
    bool hasColor = false;
    Color color{0, 0, 0, 255};       // Panel/Image/Button 背景 or Slider 塗り
    bool hasTextColor = false;
    Color textColor{0, 0, 0, 0xff};  // Label/Button テキスト色
    // 枠（2026-10-12・UE 先行）。Panel・VerticalLayout・HorizontalLayout・GridLayout・Modal・色の Image に効く。
    // borderColor 未指定はテーマの panelEdge（選んでいる間は accent のまま）。borderWidth -1 はテーマの border、0 は枠なし
    bool hasBorderColor = false;
    Color borderColor{0, 0, 0, 0xff};
    int borderWidth = -1;
    // ---- 2026-10-12 の描き方（UE 先行・rpg-themes/README.md §3.1）----
    UiGradient gradient;          // 地の濃淡（color より強い）
    std::string shape;            // "" | rect | hex | tagL | tagR | slant
    int shapeInset = -1;          // 尖りの深さ px（-1 は高さの 0.35）
    UiEdge3d edge3d;
    UiInnerBorder innerBorder;
    int slice[4] = {0, 0, 0, 0};  // Image: 9 分割（左 上 右 下・絵の px）
    bool hasSlice = false;
    bool tile = false;            // Image: 敷き詰め
    int src[4] = {0, 0, 0, 0};    // Image: 切り出し（x y w h・絵の px）
    bool hasSrc = false;
    std::string ornament;         // Divider: "diamond" | "dots" | "none"
    UiTextOutline textOutline;
    UiTextGlow textGlow;
    float letterSpacing = -1.f;   // -1 はテーマ
    bool monoDigits = false;
    bool hasHighlightTextColor = false;
    Color highlightTextColor{0, 0, 0, 255};
    int offset[2] = {0, 0};
    float rotate = 0.f;           // 度（中心を軸に中身ごと）
    float skewX = 0.f;            // 度
    bool clip = false;
    float opacity = 1.f;
    UiGlow glow;
    float backdropBlur = 0.f;

    // Label
    std::string align;     // "left"(既定) | "center" | "right"
    std::string font;      // フォント id: "main" | "bold" | "logo"。空は文脈既定
    // 文字サイズ。JSON キーは "fontSize"。**単位はポイント**（96dpi で ×4/3 px。17 → 23px。仕様 §2・2026-10-03）。
    // Slate の FSlateFontInfo::Size もポイントで、そのまま渡す。0=既定（host の既定サイズ）。
    // 親から子へは継承しない(ノード単位)。Label / Button のテキストに効く。論理座標系(1280x720)基準で、実画面へは相似に拡大される。
    int fontPt = 0;
    int wrapW = 0;         // >0 で本文の折り返し幅(px)。指定時は左寄せ・上詰めの
                           // 複数行描画。runs 指定時は run レイアウトの折り返し幅にも使う。
    // 行送り(論理px)。0=host 既定（fontPt から比で算出）。JSON キーは "lineH"。
    // wrapW 指定時の複数行描画にだけ効く。フォントサイズと行間を独立に決めたいとき用
    // （日本語は 1.5〜1.9 倍が読みやすく、フォント比の既定 30/22≒1.36 では詰まる）。
    int lineH = 0;
    // 箱(w)に収まらない文字を「…」で詰める。折り返さず1行に収めたいときに使う。
    // 幅の判定は**描画側の実フォント計測**で行う。JSON を作る側が文字幅を推定すると、
    // フォントや文字種が変わるたびに溢れる（08/13 実機で2割はみ出した）。切るのは測れる側の仕事。
    bool ellipsis = false;
    std::vector<TextRun> runs;  // 非空なら text の代わりに装飾付き run 列で描画(F1)。

    // ---- TextInput(名前入力欄 F2) ----
    // text を現在の入力バッファとして描画する(編集は host が行う。UE は SKaedeUiCanvas が重ねる SEditableText)。
    std::string placeholder;    // 空入力時に薄色で出すプレースホルダ
    std::string bindVar;        // 確定値の書き込み先 変数名(host が {var} 補間で本文へ渡す)
    int maxLen = 0;             // 最大文字数(コードポイント、0=無制限。host の編集側で使う)
    bool focused = false;       // フォーカス中(host が設定)。caret を出すかの判定に使う

    // Image
    std::string sprite;    // テクスチャパス。空なら color/panel 塗り
    std::string fit;       // "contain"(既定/枠内に収める) | "cover"(枠を埋めてクリップ)

    // Button
    std::string icon;            // テキスト前置のアイコン文字(絵文字/記号)

    // Slider / ProgressBar
    float value = 0.0f;    // 0..1
    // Toggle
    bool on = false;
    // ToggleGroup
    int selected = 0;      // 選択中の子インデックス
    // Modal
    bool dim = true;       // 背景の暗幕を出すか

    // ---- Polyline / Ellipse(F-2) ----
    // 曲線は**生成器側で折れ線に割ってから**渡す。スプラインの型を持たないのは、
    // ベジエを描く責任を JSON を作る側に置けば、描画側が MakeLines 1 本で済むため。
    std::vector<UiPoint> points;  // Polyline の頂点。2 点未満なら描かない
    float thickness = 1.0f;       // Polyline の線幅 / Ellipse の輪郭幅(fill=false のとき)
    bool closed = false;          // Polyline: 終点と始点も結ぶ(枠線を1ノードで描ける)
    bool fill = true;             // Ellipse: 塗りつぶすか(false=輪郭のみ)

    // ---- UIモーション("anim"キー。UiAnim.hpp) ----
    // enter(出現)/countUp(数値補間)/pulse(条件脈動)の宣言。nullptr=モーション無し。
    // 実行状態はツリーに持たない(毎フレーム再構築されるため)。ui/Tween.hpp の
    // UiMotionState が安定キーで外置き管理する。
    std::shared_ptr<struct UiAnimSpec> anim;

    // ---- 宣言的 repeat(動的子の宣言化) ----
    // repeat に host 提供のリスト名を指定すると、host 層(ui::expandRepeats)が
    // repeatTemplate を要素数ぶん clone し、各 clone の text/action 内 "{item.<key>}" を
    // その要素のスコープで補間して children に展開する。UiContext は関与しない。
    // 例: repeat="choices" + template Button(text "{item.index}: {item.label}")。
    // ブラウザの描画器も同じ意味で展開する（照合 Scripts/momiji-ui-conformance.ps1）。
    std::string repeat;
    std::shared_ptr<UiNode> repeatTemplate;  // "template"。展開元(不変・clone して使う)
    // 要素ごとに型を選ぶ（2026-10-03・F2）。"templates": {"<種類>": {…}} と "templateKey": "<要素のキー>"。
    // 要素の item[templateKey] が templates にあればその型、無ければ template。会話ログのように
    // 種類の違う行（入力・台詞・記録・罫）が 1 列に並ぶ一覧を JSON だけで書くため。
    std::unordered_map<std::string, std::shared_ptr<UiNode>> repeatTemplates;
    std::string templateKey;

    std::vector<UiNode> children;
};

// 鍵 k の値を T で読む。j がオブジェクトでない・鍵が無い・型が合わなければ def（例外を投げない）。
// 画面 JSON と anim の読みは全部これを通す（j.value() は型違いで例外＝画面全体が空になるので使わない）。
// T は int / float / bool / std::string（実体は UiNode.cpp）。数（int・float）は整数でも小数でも受ける
template <class T> T opt(const nlohmann::json& j, const char* k, T def);

// JSON -> UiNode (AI はこの JSON を出力すればよい)。path は j の JSON Pointer（各部品の srcPath の起点）
// 鍵の型違いは既定値に落とす（opt）。例外を投げない
UiNode nodeFromJson(const nlohmann::json& j, const std::string& path = std::string());

// ---- 画面 JSON の診断（2026-10-03・仕様 Docs/Guides/Working/momiji-ui-framework.md §12）----
// 未知の type・未知の鍵・型違い・列挙の外の値・静的な id の重複・repeat に型が無い を、場所（path）つきで返す。
// ブラウザの MomijiUICanvas.validate と同じ規則（code と path が一致する。照合が突き合わせる）。
// severity: "error"（描けない／意図どおりにならない）| "warn"（無視されるが描ける）。描くときは呼ばない（検査の道具）
struct UiDiagnostic { std::string severity, code, path, message; };
std::vector<UiDiagnostic> validateScreen(const nlohmann::json& doc);
// MOMIJIUICORE_API: KaedeVnCore（BuildPlayTree の Play 画面テンプレート）が別DLLから呼ぶ（2026-10-14 分割 S1）。
MOMIJIUICORE_API UiNode screenFromJsonString(const std::string& s);

// ---- テーマ(スキン)読み込み ----
// JSON でテーマ色を差し替える。各キーは "#rrggbb" / "#rrggbbaa"。未指定キーは base を継承。
//   { "accent": "#7ab8ff", "bg": "#0e1116", "panel": "#1a2030", ... }
// これにより「テーマ差し替えだけで別ゲーム風の見た目」を実現する(量産の差別化)。
UiTheme themeFromJson(const nlohmann::json& j, const UiTheme& base = UiTheme{});

// 組み込みプリセット。name が既知なら out にセットして true。
//   "atelier"(既定/暖色) | "midnight"(寒色) | "sakura"(桜色)
bool builtinTheme(const std::string& name, UiTheme& out);

// UTF-8 先頭バイトから 1 コードポイントのバイト長(不正な先頭は 1)。
inline int utf8CharLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

}  // namespace kaedevn::ui
