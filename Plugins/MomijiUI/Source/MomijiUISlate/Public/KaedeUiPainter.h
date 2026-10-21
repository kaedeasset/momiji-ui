// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"
#include "core/UiNode.hpp"   // kaedevn::ui::UiNode（MomijiUICore）

namespace kaedevn::ui { class UiMotionState; }   // ui/Tween.hpp（UIモーション状態）

// UiNode ツリーを「論理座標(1280x720)の描画コマンド列」に変換する（レイアウト＋描画の指示）。
// 描画(FSlateDrawElement 発行)とレイアウト計算を分離することで、
// レイアウト幾何を GUI 無しで回帰検査できる。

namespace KaedeUI
{
	enum class EKaedeDrawKind : uint8
	{
		Rect,         // 塗り矩形
		RectOutline,  // 枠線
		Text,         // テキスト（X,Y は配置済みの左上）
		Image,        // スプライト（X,Y,W,H の枠内に Sprite を Fit で収める。Phase 3）
		// ScrollView（Phase 6）: ClipPush..ClipPop で挟んだコマンドを、描画側が「ビューポート矩形で
		// クリップ＋スクロール量ぶん Y シフト」して描く。幾何は純関数のまま・スクロール量は Canvas 側が
		// id ごとに保持する（放置錬金は毎フレーム再構築するため状態はキャンバスに置く）。
		ClipPush,     // X,Y,W,H=ビューポート(論理px)・Sprite=スクロールid・ContentH=中身の総高さ
		ClipPop,      // 直近の ClipPush を閉じる
		// TextInput（Phase 6）: 編集可能な文字欄。描画側（Canvas）が自前バッファ（id 別）で本文/placeholder/
		// caret を描き、キー入力で編集する。幾何は純関数のまま・編集状態は Canvas が保持する。
		TextField,    // X,Y,W,H=テキスト領域(論理px)・Sprite=フィールドid・Text=placeholder・bBold=太字
		// 図形（F-2）: 位置と線が意味を持つ図を JSON だけで描くための最小形。
		Lines,        // Points を順に結ぶ折れ線。Thickness=線幅。曲線は生成器側で折れ線に割ってある
		Ellipse,      // X,Y,W,H=外接矩形。bFill=塗り / false なら Thickness の輪郭のみ
		// 文字の選択（2026-10-07・設計 Docs/Reports/2026/10/07/01）: selectable の器の矩形。何も描かない。
		// X,Y,W,H=器の矩形（論理px。ScrollView の中ならスクロール前の座標）・SelGroup=器の鍵・FocusGroup=当たりと同じ群。
		// ScrollView 自身が器なら ClipPush の前に出る（器は親の座標にある）
		SelectArea,
		// ---- 2026-10-12（Docs/Guides-rpg/Working/rpg-themes/README.md §3）----
		Gradient,     // X,Y,W,H・Radius・GradColors/GradPos（位置 0〜1）・bGradH（横）
		Polygon,      // Points（絶対・論理px）を塗る。PointColors があれば頂点ごとの色（濃淡）
		Blur,         // X,Y,W,H の後ろをぼかす（Blur＝強さ）
	};

	struct FKaedeDrawCmd
	{
		EKaedeDrawKind Kind = EKaedeDrawKind::Rect;
		float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;  // 論理座標(px)
		FColor Color = FColor::White;
		FString Text;          // Text のとき
		bool bBold = false;    // Text のフォント（bold/regular）
		float FontPt = 0.f;    // Text のフォントサイズ（**ポイント**。描く側が Slate にそのまま渡す＝96dpi で ×4/3 px）。0=既定。ルビ等の小サイズ用
		int32 WrapW = 0;       // >0 で折り返し幅（本文）。0=単一行
		float Radius = 0.f;    // Rect/RectOutline の角丸半径(論理px)。0=直角（従来どおり）
		// >0 のとき Text の Y は「親ボックスの上端」で、描画側が実フォント高を測って
		// ボックス内に縦センタリングする（固定オフセット -13 だとフォントによりずれるため）。
		float CenterH = 0.f;
		// Text で align=center のとき true。X は送り幅で寄せた値で、描画側が字面で寄せ直す（2026-10-02）。
		bool bCenterX = false;
		// Image のとき（Component Standard 画像拡張 CodexReports/2026/07/12-11）:
		FString Sprite;        // 画像パス（Image）/ スクロールid（ClipPush）。用途はKindで分岐
		FString Fit;           // "contain"(既定/枠内収め) | "cover"(枠埋めクリップ) | "stretch"
		float Alpha = 1.f;     // 不透明度
		float ContentH = 0.f;  // ClipPush: スクロールビューの中身の総高さ（論理px）。>viewport で縦スクロール
		// ---- UIモーション（設計 07/21/01）----
		// 非空なら描画側（SKaedeUiCanvas）が毎フレーム UiMotionState.sample(MotionKey) を引いて
		// alpha乗算/offset/scale/表示テキストを合成する。ヒット矩形は動かさない（最終位置基準）。
		FString MotionKey;
		float PivotX = 0.f, PivotY = 0.f;   // scale の基準（anim ノード矩形の中心・論理px）
		// このコマンドが属するクリック領域（OutHits の index。-1=非対話）。押下中の
		// ボタン縮小（基本の押下フィードバック。07/21 操作感）に描画側が使う。
		int32 HitIndex = -1;
		// ---- 図形（F-2）----
		// Lines: 頂点列（論理px・絶対座標）。ClipPush 中は他のコマンドと同じく Y シフトされる。
		TArray<FVector2f> Points;
		float Thickness = 1.f;  // Lines の線幅 / Ellipse の輪郭幅
		bool bFill = true;      // Ellipse: 塗りつぶすか
		// ---- TextField の操作スコープ（2026-10-04・Codex 10/04/01 F1）----
		// 配置・当たり（FKaedeHit）と同じ焦点の群と押せるか。編集器（SEditableText）は最前面の群にあって押せる欄だけに付く
		//（Modal を開いたら背面の欄の編集は降り、Enter も送らない）。見える範囲は ClipPush が持つ
		int32 FocusGroup = 0;
		bool bEnabled = true;
		// ---- 文字の選択（2026-10-07・設計 Docs/Reports/2026/10/07/01 §4.1）----
		// Text: 属する selectable の器の鍵（"id:<id>"、id が無ければ "ord:<配置順の番号>"）。空なら選べない文字（Button・TextInput・ルビ）。
		// SelRun: 同じ器の中で「同じ Label の行」を束ねる番号（Label ごとに 1 増える。同じ番号の行は改行を挟まず繋いで写す）。
		// LineH: その行の行送り（論理px。折り返さない Label は箱の高さ）。反転の高さと縦の当たりに使う
		FString SelGroup;
		int32 SelRun = -1;
		float LineH = 0.f;
		// ---- 2026-10-12（rpg-themes/README.md §3）----
		TArray<FColor> GradColors;      // Gradient の止め色
		TArray<float> GradPos;          // 止め色の位置（0〜1）
		bool bGradH = false;            // 横の濃淡
		TArray<FColor> PointColors;     // Polygon の頂点ごとの色（空なら Color）
		float OutlinePx = 0.f;          // Text の縁取りの太さ（論理px）
		FColor OutlineColor = FColor::Black;
		float LetterSpacing = 0.f;      // Text の字間（論理px）
		float Angle = 0.f, SkewX = 0.f; // 回転・斜め（度）。軸は XfX,XfY（論理px）
		float XfX = 0.f, XfY = 0.f;
		float Slice[4] = {0.f, 0.f, 0.f, 0.f};  // Image の 9 分割（左 上 右 下・絵の px）
		bool bTile = false;             // Image の敷き詰め
		float Src[4] = {0.f, 0.f, 0.f, 0.f};    // Image の切り出し（x y w h・絵の px。w>0 で有効）
		float Blur = 0.f;               // Blur の強さ
		bool bFillBar = false;          // Slider の棒（anim.tween で長さを補間する）
	};

	// CenterH 縦センタリングの字面補正（フォントサイズに対する比）。
	// FontMeasure の高さは「行ボックス」で、当時の書体（Yu Gothic）は字面が行ボックス内で下寄りに
	// 置かれるため、行ボックスを中央合わせすると字面が約 0.25em 沈む（vn-config.png の
	// ピクセル実測: 全要素一様に +5.5〜6px @22px。Docs/Reports/2026/07/12/01 参照）。
	// 描画側（SKaedeUiCanvas / SKaedeMomijiUIWidget）はこの比×フォントpxだけ持ち上げる。
	// フォントを差し替えたら再実測すること。
	//
	// ⚠ この値は 2026-07 の游ゴシックの実測値のまま（いまの同梱書体 BIZ UDP では測り直していない）。
	// 2026-10-02: 描画命令（SKaedeUiCanvas / SKaedeKscPlayWidget / SKaedeMomijiUIWidget の Text）は
	// 字面で寄せる KaedeInkCenteredTextPos に替え、この定数を使わなくなった。残っているのは
	// SKaedeMomijiUIWidget の Slate 配置（旧ノベル画面の STextBlock の位置決め）だけ。
	constexpr float KaedeTextCenterBiasEm = 0.25f;

	// テキスト幅(px, 論理)を返すコールバック。align=center/right と折返しに使う。
	//
	// FontPt<=0 は「ホストの既定サイズ」を意味する。既定値そのものは painter は持たない
	// （キャンバスは SKaedeUiCanvas::KaedeLogicalFontSize、VN 本文は SKaedeMomijiUIWidget の
	//   LogicalFontSize と、ホストごとに既定が異なるため）。0 は 0 のまま measure と
	//   FKaedeDrawCmd.FontPt の双方へ流し、解決はホストが行う＝計測と描画で必ず一致する。
	// サイズを渡さないと fontSize 指定ノードのセンタリング/折返しが既定サイズ基準に
	// なってずれるため、計測側にも必ずサイズを伝える。
	using FKaedeMeasureFn = TFunction<float(const FString& Text, bool bBold, float FontPt)>;

	// クリック可能領域（論理座標 + action 文字列）。Button が action を持つとき収集する。
	// Focus 可能領域も兼ねる（ゲームパッド/キーボード操作の土台。CodexReports/2026/07/12-13 Phase 2-3）。
	struct FKaedeHit
	{
		float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;
		FString Action;
		bool  bFocusable = true;   // UiNode.focusable を反映。Modal 背面吸収など非フォーカスは false
		int32 FocusGroup = 0;      // Modal 配下は +1。方向移動を同一グループ内に閉じ込める
		FString FocusId;           // UiNode.id（初期Focus指定 / Modal 復帰キー）
		FString ScrollId;          // ScrollView 配下の hit（空=非スクロール）。Canvas がスクロール量で
		                           // Y をずらして当たり判定し、ビューポート外はクリップ扱いで無効化する
	};

	// フォーカス移動先（各 Hit の上下左右の遷移先 index。幾何から自動生成）。-1=移動先なし。
	struct FKaedeFocusNav
	{
		int32 Up = -1, Down = -1, Left = -1, Right = -1;
	};

	// ===== UIスキン（Component Standard の部品規約切替。CodexReports/2026/07/12-09）=====
	// ComputeDrawList のスキン付きオーバーロードに渡すと、Component Standard のロール
	// （surface/card/tile/primary/ghost）をファミリの部品規約で描く:
	//   PremiumAtelier … 角丸6px＋金枠（既定。従来呼び出しと同じ見た目）
	//   Pc98           … 全直角＋二重枠（既定角丸も 0 になる）
	// JSON の atelier-*/pc98-* はロール別名として扱い、描画規約は「アクティブなファミリ」が
	// 決める（ファミリ横断で自動変換。同一JSONのままスキン一括切替するため）。
	enum class EKaedeUiSkinFamily : uint8 { PremiumAtelier = 0, Pc98 = 1 };
	struct FKaedeUiSkin
	{
		EKaedeUiSkinFamily Family = EKaedeUiSkinFamily::PremiumAtelier;
		kaedevn::ui::UiTheme Theme;        // このスキンのテーマトークン
	};

	// 部品の配置の記録（2026-10-16・UI エディタ U3。Docs/Guides-rpg/Working/rpg-ui-editor-design.md §5.2）。
	// ComputeDrawList に OutRects を渡すと、描いた（刈り込まれなかった）部品ごとに 1 件、描く順（親が先）に入る。
	// Path は部品の元の JSON の場所（UiNode::srcPath・JSON Pointer。/root/children/1/children/0）。
	// repeat で複製された部品とその中身は bFromRepeat=true・TemplatePath に元の template の Path（例 /root/children/1/template）。
	//   複製の Path は template の中の場所なので、同じ template から出た行どうしは同じ Path を持つ。
	// 矩形は配置が決めた論理座標（親の Layout が置いた位置・anchor・offset 込み。Modal は中央の窓・h の無い Label は測った高さ・
	// Polyline は頂点の外接矩形）。ScrollView の中はスクロール前の座標。Depth は根が 0
	struct FKaedeNodeRect
	{
		FString Path;
		float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;
		int32 Depth = 0;
		bool bFromRepeat = false;
		FString TemplatePath;
	};

	// UiNode ツリー → 描画コマンド列（論理座標）。anchor も解決する。
	// OutHits を渡すと action 付き Button のクリック領域も収集する。
	// OutRects を渡すと部品ごとの矩形も集める（渡さなければ今までどおり・描画命令と当たりは同じ）。
	// （デモツリーの生成は KaedeCore::BuildDemoPlayTree。Core 関数を呼ぶため Core 側に置く。）
	MOMIJIUISLATE_API TArray<FKaedeDrawCmd> ComputeDrawList(
		const kaedevn::ui::UiNode& Root, int32 ViewportW, int32 ViewportH,
		const FKaedeMeasureFn& Measure, TArray<FKaedeHit>* OutHits = nullptr,
		const kaedevn::ui::UiTheme* ThemeOverride = nullptr, TArray<FKaedeNodeRect>* OutRects = nullptr);

	// ===== ScrollView のスクロールバー（溝とつまみの寸法。論理px）=====
	// **描画と当たり判定が同じ数式を見るための一か所。** 2026-09-20 まで寸法は
	// SKaedeUiCanvas::OnPaint の中にしか無く、掴む側を書こうとすると必ず二重に持つことになった
	// （設計 Docs/Reports/2026/09/20/05 §2.2）。純関数なので MomijiUISlateTests から直に測れる。
	struct FKaedeScrollBar
	{
		float TrackX = 0.f, TrackY = 0.f, TrackW = 0.f, TrackH = 0.f;   // 溝（描くのはこの幅）
		float ThumbY = 0.f, ThumbH = 0.f;                               // つまみ
		float HitX = 0.f, HitW = 0.f;                                   // 掴める幅（描画より広い）
		float MaxScroll = 0.f;                                          // max(0, ContentH − VpH)
	};
	// 中身がビューポートに収まっている（MaxScroll <= 0）なら false＝バーは無い。
	// Vp* はビューポートの論理矩形、Offset は現在のスクロール量。
	MOMIJIUISLATE_API bool ScrollBarGeometry(float VpX, float VpY, float VpW, float VpH,
	                                   float ContentH, float Offset, FKaedeScrollBar& Out);

	// ===== 本文（wrapW ノード）の折り返し =====
	// 描画と同じ貪欲折り返しを外から呼ぶための口。**木へ h を先に入れる側（host）が、
	// 描画と同じ関数で行数を数えるため**に公開している。文字数からの推定で高さを決めると、
	// 実測の折り返しと食い違って行が重なる（2026-09-20 実測・応答パネル）。
	// 単位は描画と同じ論理px。FontPt / WrapW は木に入っている値（相似変換後）をそのまま渡す。
	MOMIJIUISLATE_API TArray<FString> WrapTextToLines(const FString& Text, int32 WrapW, bool bBold,
	                                            float FontPt, const FKaedeMeasureFn& Measure);

	// wrapW ノードの行送り（描画が使う値と同じ。fontPt<=0 はホスト既定の行送り）。
	MOMIJIUISLATE_API int32 WrappedLineHeight(int32 FontPt);

	// ===== UIモーション（"anim" キー。設計 Docs/Reports/2026/07/21/01）=====
	// ComputeDrawList に渡すと、anim 付きノードへ安定キーを振って UiMotionState へ登録し
	//（enter 発火・countUp 値検知・pulse 条件評価）、そのノード配下の描画コマンドに
	// MotionKey/Pivot を付与する。時間進行はホストの Tick（UiMotionState.update）、
	// 合成は描画側（SKaedeUiCanvas::OnPaint）が毎フレーム行う＝Rebuild 頻度は据え置き。
	struct FKaedeMotionCtx
	{
		kaedevn::ui::UiMotionState* State = nullptr;
		FString ScreenId;                              // 安定キーの接頭辞（画面遷移の判定にも使う）
		TFunction<bool(const std::string&)> EvalCond;  // pulse whileIf の評価（ホストの VarEnv で evalCond）
	};

	// スキン付きオーバーロード（テーマは Skin.Theme を使う）。既存呼び出しは上の版のまま
	// = PremiumAtelier 相当で動く（API 非破壊）。Motion は任意（nullptr=モーション無し）。
	MOMIJIUISLATE_API TArray<FKaedeDrawCmd> ComputeDrawList(
		const kaedevn::ui::UiNode& Root, int32 ViewportW, int32 ViewportH,
		const FKaedeMeasureFn& Measure, TArray<FKaedeHit>* OutHits,
		const FKaedeUiSkin& Skin, const FKaedeMotionCtx* Motion = nullptr, TArray<FKaedeNodeRect>* OutRects = nullptr);

	// ===== bindVar 状態バインド（設定画面/タイトル等の宣言的画面をプレビューと
	//        ランタイム(SKaedeMomijiUIWidget)で同じ意味で動かすための共有ヘルパ）=====

	// bindVar 付きウィジェットの初期値（JSON の value/on/selected）を変数マップへ
	// 流し込む（未登録キーのみ。JSON の静的値が「既定値」の出所になる）。
	MOMIJIUISLATE_API void SeedVarsFromTree(const kaedevn::ui::UiNode& Node,
	                                  std::unordered_map<std::string, double>& Vars);

	// 画面 action の接頭辞を外す。In が Prefix で始まれば残りを OutRest に入れて true（手で数えた Mid(N) の代わり）。
	MOMIJIUISLATE_API bool TakePrefix(const FString& In, const TCHAR* Prefix, FString& OutRest);

	// action が Slider（slider:<var>）か。押下で即反映＋ドラッグする当たりの見分けに使う。
	MOMIJIUISLATE_API bool IsSliderAction(const FString& Action);

	// 変数の action（set:<var>=<n> / slider:<var> / toggle:<var> / reset）を Vars へ当てる。
	// ノベルの画面・ADV の外殻・プレビューが同じこの 1 本を呼ぶ（保存・再描画は呼んだ側）。
	// reset は DefaultsTree の既定値（SeedVarsFromTree）で上書きする。当てなければ None。OutName は set/slider/toggle の変数名
	enum class EKaedeVarAction : uint8 { None, Set, Slider, Toggle, Reset };
	MOMIJIUISLATE_API EKaedeVarAction ApplyShellVarAction(const FString& Action, float RelX,
	                                  const kaedevn::ui::UiNode& DefaultsTree,
	                                  std::unordered_map<std::string, double>& Vars, FString* OutName = nullptr);

	// 変数マップの現在値を bindVar 付きウィジェット（Slider/Toggle/ToggleGroup）へ反映する。
	MOMIJIUISLATE_API void ApplyVarsToTree(kaedevn::ui::UiNode& Node,
	                                 const std::unordered_map<std::string, double>& Vars);

	// action が一致するノードの enabled を書き換える（例: セーブが無いとき "continue" を
	// 無効化・空スロットの "load:3" を無効化）。一致した個数を返す。
	MOMIJIUISLATE_API int32 SetActionEnabled(kaedevn::ui::UiNode& Node,
	                                   const FString& Action, bool bEnabled);

	// Focus Graph（Phase 3）: 同一 FocusGroup 内の focusable hit について、各方向で中心が正しい側に
	// あり主軸距離＋副軸ペナルティが最小の hit を遷移先にする（縦リスト/横タブ/グリッドに自然対応）。
	// 純関数（GUI 非依存でテスト可能）。戻り値は Hits と同 index。
	MOMIJIUISLATE_API TArray<FKaedeFocusNav> BuildFocusNav(const TArray<FKaedeHit>& Hits);

	// 部品の鍵（仕様 Docs/Guides/Working/momiji-ui-framework.md §7・2026-10-03）: id があれば "id:<id>"、無ければ "action:<action>"。
	// 焦点・状態はこの鍵で追う（action は「何をするか」であって部品の同一性ではない）。ブラウザの hitKey と同じ
	MOMIJIUISLATE_API FString HitKey(const FKaedeHit& Hit);

	// 部品の高さを描かずに測る（仕様 §5。値を差し込み済みの部品を渡す）。配置が使うのと同じ式（MeasureStackH）。
	// 折り返す Label は Measure で行数を数える。host が「次へ」のページ割りなどで使う。ブラウザの Renderer.measureNode と同じ
	MOMIJIUISLATE_API int32 MeasureNodeHeight(const kaedevn::ui::UiNode& Node, const FKaedeMeasureFn& Measure);
}
