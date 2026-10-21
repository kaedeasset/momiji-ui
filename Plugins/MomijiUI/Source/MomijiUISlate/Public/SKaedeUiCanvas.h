// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/StrongObjectPtr.h"   // TStrongObjectPtr（Image テクスチャの GC 保護）
#include "KaedeUiPainter.h"   // FKaedeDrawCmd / FKaedeHit
#include "ui/Interp.hpp"      // kaedevn::ui::ItemScope（PresentScreen の一覧）
#include <unordered_map>
#include <vector>

// SKaedeUiCanvas — ComputeDrawList が返した描画コマンド列を論理座標→レターボックスで
// 描く共有リーフウィジェット。クリック領域(FKaedeHit)を持ち、クリック/ドラッグ(Slider)を
// action 文字列でオーナーへ通知する。エディタプレビュー(SKaedeMomijiUIPreview)とランタイムの
// 宣言的UI画面（アルケミスト等）が共用する（元は preview 内部クラス SMomijiUICanvas）。

class UTexture2D;
namespace kaedevn::ui { struct UiMotionSample; }   // ui/Tween.hpp（画面遷移エンターの sample）

namespace KaedeUI
{
	// キャンバスの中の仕事を分けた先（Private。2026-10-18 Issue #73）
	struct FKaedeTextSelection;   // 文字の選択の状態と規則（KaedeTextSelection.h）
	struct FKaedeSelEnv;
	struct FKaedePaintCtx;        // OnPaint の描き先（KaedeUiCanvasDraw.h）
	struct FKaedeLayerStack;      // 重ね順の段（同上）
}

namespace KaedeUI
{
	// フォント系統。Gothic＝BIZ UDP ゴシック（既定）・Mincho＝BIZ UDP 明朝・
	// Hand＝手書き風（Yomogi）・Dot＝ドット書体（DotGothic16）。Hand と Dot は太字の版が無い＝同じ書体。
	// どの画面がどれを使うかは使う側（SKaedeUiCanvas::SetFontFamily・MakeKaedeUiFont の引数）が決める。
	enum class EKaedeFontFamily : uint8 { Gothic = 0, Mincho = 1, Hand = 2, Dot = 3 };
	// 系統の数（書体の控えの配列の大きさ）。系統を足したら最後の値に合わせる
	inline constexpr int32 KaedeFontFamilyCount = (int32)EKaedeFontFamily::Dot + 1;

	// 日本語対応のUIフォント。2026-10-02 から同梱フォント（Plugins/MomijiUI/Resources/Fonts の
	// BIZ UDP ゴシック／明朝ほか・SIL OFL 1.1）だけを読む。OS のフォントは見ない。
	MOMIJIUISLATE_API FSlateFontInfo MakeKaedeUiFont(bool bBold, int32 SizePx,
	                                           EKaedeFontFamily Family = EKaedeFontFamily::Gothic);

	// 文字の字面（実際にインクが乗る範囲）。文字ブロックの左上を原点にした px。
	// ボタン等の中央寄せはこれで字面の中心を箱の中心へ合わせる（2026-10-02）。
	// 以前は行の高さ＋游ゴシック実測の固定補正（0.25em）で寄せていて、フォントを差し替えると上下にずれ、
	// 横も送り幅（左右の空き込み）で寄せていたので字面は中心から外れていた。
	struct FKaedeTextInk
	{
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
		float Advance = 0.f;   // 送り幅（従来の Measure().X 相当）
		bool bValid = false;   // 空文字・初期化前・字形が取れないときは false
	};
	MOMIJIUISLATE_API FKaedeTextInk MeasureKaedeTextInk(const FString& Text, const FSlateFontInfo& Font);

	// 箱の中へ字面で中央寄せした描画位置を返す。Pos は描画側の位置（X=送り幅で寄せた文字の左、Y=箱の上端）、
	// BoxH は箱の高さ（描画 px）。bCenterX のとき X も字面の中心を箱の中心（＝送り幅で寄せた中心）に合わせる。
	// 字面が取れないときは行の高さで縦に寄せる。
	MOMIJIUISLATE_API FVector2f KaedeInkCenteredTextPos(const FString& Text, const FSlateFontInfo& Font,
	                                               FVector2f Pos, float BoxH, bool bCenterX);

	// 角丸描画用のブラシ（FSlateRoundedBoxBrush）。MakeBox は brush ポインタを保持するため
	// 永続キャッシュから返す（半径は整数px量子化）。Fill=白塗り（Tint で着色）、
	// Outline=透明塗り+枠色（枠色は Tint が乗らないためブラシに焼き込む）。
	MOMIJIUISLATE_API const FSlateBrush* GetKaedeRoundedFillBrush(float RadiusPx);
	MOMIJIUISLATE_API const FSlateBrush* GetKaedeRoundedOutlineBrush(float RadiusPx, const FLinearColor& Color, float Width);

	// PNG ファイル → FSlateBrush（RGBA そのまま。アルファ付き立ち絵用）。
	// OutTexture に生成テクスチャを返すので、呼び出し側が TStrongObjectPtr 等で GC から保護すること。
	// 失敗時は nullptr。
	MOMIJIUISLATE_API TSharedPtr<FSlateBrush> KaedeLoadPngBrush(const FString& FilePath, ::UTexture2D*& OutTexture, bool bNearest = false);   // bNearest: ドット絵（最近傍・2026-10-12）

	// キャンバスの「スプライトレイヤ」1枚（論理座標）。背景の直後・UIコマンドの前に描く。
	struct FKaedeCanvasSprite
	{
		TSharedPtr<FSlateBrush> Brush;
		float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;
	};

	// **基準の論理サイズ**（1280x720）。VN 本文テンプレ kPlayJson の
	// 絶対座標がこの値ベタ書きで、Kaede.UI.PlayLayout 契約テストが固定しているため
	// **この値は変えない**。個々のキャンバスは SLATE_ARGUMENT で上書きできる。
	//
	// 注意: これは「VN の既定解像度」ではない。VN の既定は下の KaedeVnLogical*（1920x1080）。
	// MakePlayJson(W,H) が kPlayJson をこの基準から相似変換するので、1080p でも
	// 配置は相似形として保たれる（Kaede.UI.PlayLayout1080p が 1.5 倍を検証）。
	static constexpr int32 KaedeLogicalW = 1280;
	static constexpr int32 KaedeLogicalH = 720;

	// VN（ノベル）の既定論理解像度。2026-07-28 に UE 側を 1080p へ統一した。
	// 素材を 1920x1080 で作って等倍で出すのが目的（設計 Docs/Reports/2026/07/25/02）。
	// 16:9 同士の相似なので、1280x720 基準の座標・フォントは全部 1.5 倍で移る。
	// 720p で出したい作品だけがシナリオ JSON に "screen": "720p" を書く。
	static constexpr int32 KaedeVnLogicalW = 1920;
	static constexpr int32 KaedeVnLogicalH = 1080;
	// キャンバス既定の文字サイズ（論理px）。JSON の "fontSize" 未指定ノードがこれで描かれる。
	// 論理1280 → 実画面1920 は 1.5倍なので、16 は実測 24px 相当（1280系キャンバスの場合）。
	//
	// 22（実測33px＝ダンジョントラベラーズ級の大きさ）から 16 へ縮小した。
	// 方針: Docs/Reports/2026/07/15/01 — 放置遠征隊に追従・PC(モニタ)専用・高密度。
	// これより小さくしたい行は JSON 側で "fontSize": 12〜14 を指定する（12 未満は
	// 漢字が潰れるので使わないこと。同レポート「日本語フォントは16px下限」を参照）。
	static constexpr int32 KaedeLogicalFontSize = 16;
}

// TextInput の編集は ChildSlot に置いた本物の SEditableText が担う（IME・選択・クリップボードのため）。
// そのため SLeafWidget ではなく SCompoundWidget。子は「編集中のフィールドの矩形」1個だけで、
// 位置は OnArrangeChildren が描画と同じレターボックス式から毎フレーム決める。
class MOMIJIUISLATE_API SKaedeUiCanvas : public SCompoundWidget
{
public:
	// (action, relX) を通知。relX はヒット矩形内の相対X(0..1、Slider のドラッグ位置用)。
	using FOnAction = TFunction<void(const FString& Action, float RelX)>;

	// 論理キャンバスの寸法と既定文字サイズ。既定は 1280x720/16（KaedeLogicalW/H と KaedeLogicalFontSize）。
	// アルケミストは 1920x1080/24 を渡す＝1論理px が 1080p 実画面の 1物理dot に一致し、
	// レイアウト値をスクショ実測値のまま書ける（設計 07/27/03 §0）。
	// FontFamily の既定は Gothic（BIZ UDP ゴシック）。ほかの系統は使う側が渡す。
	SLATE_BEGIN_ARGS(SKaedeUiCanvas)
		: _LogicalW(KaedeUI::KaedeLogicalW)
		, _LogicalH(KaedeUI::KaedeLogicalH)
		, _FontSize(KaedeUI::KaedeLogicalFontSize)
		, _FontFamily(KaedeUI::EKaedeFontFamily::Gothic)
	{}
		SLATE_ARGUMENT(int32, LogicalW)
		SLATE_ARGUMENT(int32, LogicalH)
		SLATE_ARGUMENT(int32, FontSize)
		SLATE_ARGUMENT(KaedeUI::EKaedeFontFamily, FontFamily)
	SLATE_END_ARGS()

	SKaedeUiCanvas();
	virtual ~SKaedeUiCanvas() override;   // 文字の選択（不完全型の TUniquePtr）を cpp で捨てる
	void Construct(const FArguments& InArgs);

	int32 GetLogicalW() const { return LogicalW; }
	int32 GetLogicalH() const { return LogicalH; }
	int32 GetFontSize() const { return FontSize; }
	// 書体はプレイヤー設定で変わるので、生成後にも差し替えられるようにしておく。
	void SetFontFamily(KaedeUI::EKaedeFontFamily F) { FontFamily = F; }

	void SetContent(TArray<KaedeUI::FKaedeDrawCmd> InCmds, TArray<KaedeUI::FKaedeHit> InHits, FLinearColor InBg);
	// UIモーション状態（設計 07/21/01）。設定すると OnPaint が MotionKey 付きコマンドへ
	// alpha/offset/scale/表示テキストを毎フレーム合成する。時間進行（update）はホストの Tick。
	// 生ポインタ: 所有はホスト（埋め込み先ウィジェットのメンバ）で、キャンバスより長生きする前提。
	void SetMotionState(kaedevn::ui::UiMotionState* InState) { MotionState = InState; }
	// ドット絵: 画像を最近傍で拡大する（滲ませない・2026-10-12・テーマの pixelArt）
	void SetPixelArt(bool b);
	// 焦点（キー操作で選んでいる部品）の見た目（2026-10-12・テーマの select と accent）。
	// Cursor があれば枠の代わりに部品の左の外へ絵を置く。bText は文字の色・bFill は部品の地の色を替える
	struct FFocusStyle
	{
		FLinearColor Ring = FLinearColor::FromSRGBColor(FColor(0xe0, 0xa0, 0x4a, 0xff));
		bool bText = false; FColor Text = FColor::White;
		bool bFill = false; FColor Fill = FColor::White;
		TArray<FColor> GradColors; TArray<float> GradPos; bool bGradH = false;   // 選んだ項目の地の濃淡（あれば Fill より先）
		bool bBorder = false; FColor Border = FColor::White;                        // 文字だけの項目に地を敷くときの枠
		FString Cursor; float CursorW = 0.f, CursorH = 0.f, CursorDx = 0.f, CursorDy = 0.f;
	};
	void SetFocusStyle(const FFocusStyle& S);
	// 画面遷移エンター（B3・07/21/02）の適用範囲: Cmds の先頭からこの個数だけが「画面コンテンツ」。
	// それ以降（HUD/トースト）は固定＝タブ切替でチラつかない。-1=全コマンド対象・0=適用なし。
	void SetScreenCmdNum(int32 InNum) { ScreenCmdNum = InNum; }
	// スプライトレイヤ（2Dキャラ等）。背景ベタ塗りの直後・UIコマンドの前に描く。空=無し。
	void SetSprites(TArray<KaedeUI::FKaedeCanvasSprite> InSprites);

	// 実行時に生成した画像を Image ノードから使えるようにする。
	// 画面JSON の "sprite" にここで登録したキー（例 "dyn:pc98preview"）を書くと、
	// ファイル読込ではなくこのブラシが使われる。毎フレーム作り直す動的プレビュー用。
	// Brush が無効なら登録を消す。
	void SetDynamicBrush(const FString& Key, TSharedPtr<FSlateBrush> Brush);
	void SetOnAction(FOnAction InOnAction) { OnAction = MoveTemp(InOnAction); }
	// いま押せる部品の当たり（最後に SetContent したもの）。テストや host の確認用（読むだけ）
	const TArray<KaedeUI::FKaedeHit>& GetHits() const { return Hits; }
	// いま描いている命令（最後に SetContent したもの・論理座標）。テストや host の確認用（読むだけ）
	const TArray<KaedeUI::FKaedeDrawCmd>& GetCmds() const { return Cmds; }

	// UIのヒット領域に当たらないクリック（＝背景＝3Dワールド）を受け取る。座標はこのキャンバスの論理座標。
	// true を返すと消費（Handled）、false なら従来どおり Unhandled で下へ流す。
	// 用途: ホスト側で背景の3Dオブジェクトのクリックから別画面を開く（3Dノベル/クラフト等）。
	using FOnBackgroundClick = TFunction<bool(const FVector2D& LogicalPos)>;
	void SetOnBackgroundClick(FOnBackgroundClick In) { OnBackgroundClick = MoveTemp(In); }

	// ===== Focus / 状態モデル（Phase 2-3。ゲームパッド/キーボード操作の土台）=====
	// フォーカスを Hits の index で持つ。-1 = 無し。SetContent で最上位グループの先頭へ自動初期化。
	int32 GetFocusedIndex() const { return FocusedIndex; }
	void SetFocusedIndex(int32 Index);
	// 方向移動（Focus Graph。dx/dy は -1/0/+1）。移動できたら true。
	bool MoveFocus(int32 dx, int32 dy);
	// 現在フォーカス中の action（無ければ空）。Confirm 実行に使う。
	FString FocusedAction() const;
	// 現在フォーカス中の部品の鍵（KaedeUI::HitKey。無ければ空）。焦点はこの鍵で追う（仕様 §7・2026-10-03）
	FString FocusedKey() const;
	int32 GetTopFocusGroup() const { return TopFocusGroup(); }
	// 論理座標で押せる部品の鍵（スクロールの見える範囲の外は押せない。無ければ空）。照合の口
	FString HitKeyAt(FVector2f L) const;
	// スクロール量（クランプ済み）と、いま画面にある ScrollView の id（照合の口）
	float GetScrollOffset(const FString& Id) const { return ScrollOffsetFor(Id); }
	TArray<FString> ScrollIds() const { TArray<FString> Out; ScrollViewports.GetKeys(Out); return Out; }
	// フォーカスリングの表示可否（ゲームパッド/キーボード操作中のみ true。マウス時は Hover のみ）。
	void SetFocusVisible(bool bVisible) { bFocusVisible = bVisible; Invalidate(EInvalidateWidgetReason::Paint); }
	// マウスを乗せた項目へ焦点を移す（印は焦点の 1 つだけ・乗せた所を白くしない）。RPG のゲームの画面が使う（2026-10-12）。
	// 既定は off（今までどおり: マウスを乗せた所を白く光らせ、焦点の印は消す）
	void SetHoverMovesFocus(bool b) { bHoverMovesFocus = b; }
	bool IsFocusVisible() const { return bFocusVisible; }
	// 最上位グループの先頭 focusable へフォーカスを寄せる（画面/Modal を開いた直後の初期Focus）。
	void ResetFocusToFirst();

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(640.0, 360.0); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// 連打の2回目は Slate がダブルクリックとして送るため、押下として転送する（さもないと
	// 1回おきにクリックが消える。設計 07/21/04 §C2）。連打は主ユースケース。
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// Alt+Tab 等でキャプチャが切れたときに押下表示が固着しないよう状態を落とす（同 §C2）。
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	// 選べる文字の上では I ビーム（文字の選択・2026-10-07）
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	// ===== 文字の選択とコピー（2026-10-07・設計 Docs/Reports/2026/10/07/01・仕様 momiji-ui-framework.md §7「選択」）=====
	// selectable の器の中の Label の文字をマウスのドラッグで選び、離した瞬間にクリップボードへ入れる（端末方式）。
	// 位置は (行, 文字番号)。行は器の中の選べる行の描画順の番号、文字番号はコードポイントで 0〜文字数
	struct FKaedeSelectionInfo
	{
		FString Group;                 // 器の鍵（"id:<id>" / "ord:<n>"）
		int32 FromLine = 0, FromCh = 0;
		int32 ToLine = 0, ToCh = 0;    // From <= To（描画順に並べたもの）
		FString Text;                  // 写す文（§4.5）
	};
	// いまの選択（無ければ false）。照合と probe-select が読む
	bool GetSelection(FKaedeSelectionInfo& Out) const;
	// `group=<鍵> from=<行>,<字> to=<行>,<字> text=<文>`（選択が無ければ `none`）。probe-select
	FString DescribeSelection() const;
	// 検証の口: P0 で押し、P1 へ動かし、P1 で離す（OnMouseButtonDown / Move / Up と同じ関数。合成 OS クリックは Slate に届かないため）
	void SelectTextProbe(FVector2f P0, FVector2f P1);
	void ClearSelection();

	// ===== TextInput（Phase 6）: 編集バッファは id 別にキャンバスが保持（再構築で消えない）=====
	//
	// **編集そのものは ChildSlot の SEditableText が行う。** 自前でキー入力を溜めていた頃は
	// IME を通らず日本語が打てなかった（2026-08-10 修正）。UE で IME に対応するには
	// ITextInputMethodContext の実装が要り、それは実質 SEditableText の再実装になる。
	// バッファ（TextBuffers）は id 別に持ち続けるので、外から見た API は変わっていない。
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void SetTextInputValue(const FString& Id, const FString& Value);
	FString GetTextInputValue(const FString& Id) const
	{ const FString* V = TextBuffers.Find(Id); return V ? *V : FString(); }
	FString ActiveTextInput() const { return ActiveTextField; }
	// その欄がいま画面に出ているか（描画コマンドに TextField があるか）。
	// prune された欄（面の切替・「文字を消す」）へフォーカスを渡さないために、外から引けるようにする。
	bool HasTextInput(const FString& Id) const;
	// 方向キー（パッド）で最上位グループの押せるものへ全部届くか（2026-09-12）。**合否をログの数で言うため**。
	// MomijiUI.FocusReachability と同じ規則を、いま画面に出ている本物の当たり判定に掛ける。
	FString DescribeFocus() const;
	// フィールドをプログラムから編集フォーカスへ（ダイアログを開いた直後など）。空で編集解除。
	// **操作できない欄（押せない・Modal の背面・ScrollView で隠れている）には付かない**（2026-10-04・仕様 §6「操作層」）。付いたら true
	bool SetActiveTextInput(const FString& Id);

	// ===== 編集器の操作スコープ（2026-10-04・Codex 10/04/01 F1/F2）=====
	// 文字欄がいま操作できるか: 押せる ∧ 最前面の群（Modal が開いていればその中）∧ ScrollView の見える部分がある。
	// 配置（TextField の FocusGroup/bEnabled）・当たり（HitAtLogical）・編集器（SEditableText）が同じ条件を見る。
	// 描き直し（SetContent）とスクロールのたびに ReconcileTextEditor がこれで確かめ、偽になった欄の編集は降りる
	bool IsTextInputInteractive(const FString& Id) const;
	// 欄の枠の見える矩形（論理座標・ScrollView のクリップとの交わり）。欄が無い・見える部分が無ければ false
	bool TextInputVisibleRect(const FString& Id, FVector2f& OutPos, FVector2f& OutSize) const;
	// 編集中の欄の見える矩形（編集していなければ false）。OnPaint はこの範囲で SEditableText を切る＝枠外は描かれず押せない
	bool TextEditorVisibleRect(FVector2f& OutPos, FVector2f& OutSize) const { return TextInputVisibleRect(ActiveTextField, OutPos, OutSize); }
	// 検証の口: 編集中の欄へ文字が打たれたのと同じ経路（SEditableText の文字が変わる → バッファへ）。編集していなければ何もせず false
	bool TypeIntoActiveTextInput(const FString& Text);
	// 検証の口: 論理座標で押して離す（OnMouseButtonDown / Up と同じ関数。合成 OS クリックは Slate に届かないため）
	void ClickAtProbe(FVector2f L);
	// 検証の口: 論理座標にある操作できる文字欄の id（無ければ空）。押下が編集に入るかどうかを決めるのと同じ関数（TextFieldAt）
	FString TextInputIdAt(FVector2f L) const { return TextFieldAt(L); }
	// いま画面に出ている文字欄の id（描画命令の TextField の順）。検証の口
	TArray<FString> TextInputIds() const;

	// ===== 画面ごとの状態（2026-10-04・Codex 10/04/01 F3・仕様 §6「画面の状態」）=====
	// 文字欄の中身（TextBuffers）・スクロール量・焦点の鍵と控え（FocusStack）は**画面の名前ごと**に持つ。
	// 画面が変わると（PresentScreen の ScreenId、または host の SetScreenId）その画面の状態に切り替わる＝別の画面の同じ id/bindVar と混ざらない。
	// 戻れば前の状態。捨てるのは ResetScreenState（画面を作り直す・はじめからやり直す）。
	// 画面をまたいで共有したい値は host が持ち、描く前に SetTextInputValue / SetScrollOffset で入れる（明示的な共有）。
	// SetScreenId を呼ばない host（名前が常に空）は従来どおり 1 つの状態。
	void SetScreenId(const FString& Id);
	const FString& GetScreenId() const { return ScreenId; }
	void ResetScreenState(const FString& Id);   // いまの画面なら空の状態になる（編集中の欄は降りる）
	void ResetAllScreenState();
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry,
		FArrangedChildren& ArrangedChildren) const override;

private:
	// 論理キャンバス寸法（Construct で確定・以後不変）。
	int32 LogicalW = KaedeUI::KaedeLogicalW;
	int32 LogicalH = KaedeUI::KaedeLogicalH;
	int32 FontSize = KaedeUI::KaedeLogicalFontSize;
	KaedeUI::EKaedeFontFamily FontFamily = KaedeUI::EKaedeFontFamily::Gothic;

	// レターボックス: 論理キャンバスを与えられた大きさへ縦横比を保って中央に収める。**式はここ 1 つ**
	//（描画・入力欄の配置・入力の逆変換が同じ値を見る。ずれると入力欄が枠から外れる）
	struct FLetterbox { float Scale = 1.f, OffX = 0.f, OffY = 0.f; };
	FLetterbox LetterboxFor(FVector2f Size) const;
	// 最後に描いた倍率（未描画は 1）。入力欄の文字の大きさと文字の選択の測り方が描画と同じ値を見る
	float PaintedScale() const;
	// スクリーン座標 → 論理座標（OnPaint と同じレターボックスの逆変換）。
	FVector2f ToLogical(const FGeometry& Geo, const FPointerEvent& Ev) const;

	// Cmds（bWithGhosts なら続けて Ghosts）を ClipPush/ClipPop を追いながら歩く**唯一の所**。
	// Fn(番号, 命令, Clip) の Clip はその命令が入っている ScrollView の ClipPush（外なら nullptr）。ClipPush と ClipPop の命令にも
	// 自分の ClipPush が渡る。Fn が false を返すと止まる。描画・当たり・文字欄・文字の選択がこれで同じ追い方をする
	void WalkCmds(bool bWithGhosts, TFunctionRef<bool(int32 Index, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)> Fn) const;

	// Image 描画コマンドの sprite パス → ブラシ（Phase 3）。読むのは SetContent・SetPixelArt・SetFocusStyle（LoadImageBrushes）で、
	// 描画（ResolveImageBrush）は控えを引くだけ＝OnPaint の中でファイルを読まない（2026-10-18・レビュー R2-MOMIJI-09）。
	// 値が空 TSharedPtr のエントリ = 解決失敗を記録（失敗は 2 秒たってから次の読み込みで再試行・後置き PNG の復帰用）。
	// 生成テクスチャは GC 保護のため保持する。
	const FSlateBrush* ResolveImageBrush(const FString& Sprite) const;
	void LoadImageBrushes();                    // いまの Cmds・Ghosts の Image と焦点のカーソルの絵を読む
	void LoadImageBrush(const FString& Sprite);
	FString ImageCacheKey(const FString& Sprite) const;   // ドット絵は別のテクスチャ
	TMap<FString, TSharedPtr<FSlateBrush>> ImageBrushCache;
	TMap<FString, TSharedPtr<FSlateBrush>> DynamicBrushes;   // SetDynamicBrush 登録分（ファイルより優先）
	TMap<FString, double> ImageBrushFailAt;   // 読込失敗の時刻
	TArray<TStrongObjectPtr<UTexture2D>> ImageTextures;  // GC 保護

	// ---- 描画（OnPaint）の部品。描き方そのものは Private/KaedeUiCanvasDraw.cpp ----
	// 命令 1 つの写しへ、スクロール・画面遷移エンター・押下/ホバー/フラッシュ・焦点の見た目・UIモーションを合成する。
	// 焦点の地を敷く項目は P・Layer に地を描く。ScreenSE はこのフレームの画面遷移エンター（無ければ nullptr）。FbMul/FbWhite は画像に掛ける明るさ
	KaedeUI::FKaedeDrawCmd ComposeCmd(int32 CmdIdx, const KaedeUI::FKaedeDrawCmd& C0, float ScrollOffset, const kaedevn::ui::UiMotionSample* ScreenSE,
		const KaedeUI::FKaedePaintCtx& P, int32 Layer, bool& bFocusFillDone, float& OutFbMul, float& OutFbWhite) const;
	void DrawCmd(const KaedeUI::FKaedePaintCtx& P, KaedeUI::FKaedeLayerStack& Layers, const KaedeUI::FKaedeDrawCmd& C, float FbMul, float FbWhite) const;
	void PaintSelection(const KaedeUI::FKaedePaintCtx& P, int32 Layer) const;   // 文字の選択の反転と「コピーしました」の札（KaedeTextSelection.cpp）
	// 最後に ChildSlot（編集中の TextInput）を描く。欄が ScrollView の中ならその見える範囲で切る
	int32 PaintTextEditor(const FPaintArgs& Args, const KaedeUI::FKaedePaintCtx& P, const FSlateRect& MyCullingRect,
		int32 Layer, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;

	// フォーカス移動先（各 Hit の上下左右の遷移先 index。KaedeUI::BuildFocusNav が幾何から自動生成）。
	int32 TopFocusGroup() const;            // 最大 FocusGroup（Modal 配下が最前面）
	int32 HitAtLogical(FVector2f L) const;  // 論理座標の最前面 hit index（-1=無し・スクロール量考慮）

	// ===== ScrollView（Phase 6）: スクロール量は id ごとにキャンバスが保持（再構築で消えない）=====
	// SetContent 後に Cmds の ClipPush から収集。offset は wheel/方向で更新し、描画/当たり判定で適用する。
	TMap<FString, float> ScrollOffsets;              // id → スクロール量（論理px・下方向+）
	TMap<FString, FVector4f> ScrollViewports;        // id → ビューポート(x,y,w,h)（当該フレーム）
	TMap<FString, float> ScrollContentH;             // id → 中身の総高さ
	void CollectScrollRegions();                     // Cmds から上記を再構築し offset をクランプ
	float ScrollOffsetFor(const FString& Id) const;  // クランプ済みスクロール量（未登録は0）
public:
	void SetScrollOffset(const FString& Id, float Px);   // 検証/復元用にスクロール量を設定（いまの画面）
private:
	void OnScrollChanged();   // スクロール量が変わった: 編集中の欄が隠れたら降りる（ReconcileTextEditor）・描き直す
	float ScrollMaxFor(const FString& Id) const;     // max(0, contentH - viewportH)
	FString ScrollViewAt(FVector2f L) const;         // 論理座標を含む ScrollView の id（無=空）
	// ホイール 1 ノッチ＝論理高の 56/720（1280x720 で 56px・1920x1080 で 84px）。論理解像度が変わっても「画面比で同じ量」動く
	float WheelNotch() const { return (float)LogicalH * (56.f / 720.f); }
	void EnsureScrollVisible(int32 HitIndex);        // フォーカス中 hit をビューポート内へ収める

	// ===== スクロールバーを掴む（2026-09-20・設計 09/20/05 §2）=====
	// 寸法は KaedeUI::ScrollBarGeometry（純関数）だけが持ち、描画も当たり判定もそれを呼ぶ。
	bool ScrollBarFor(const FString& Id, KaedeUI::FKaedeScrollBar& Out) const;
	// 論理座標が掴める幅に入っている ScrollView の id（無=空）。つまみの上なら *bOutOnThumb=true。
	FString ScrollBarAt(FVector2f L, bool* bOutOnThumb = nullptr) const;
	// つまみを掴んだまま Y へ動かしたときのスクロール量を入れる（掴んだ位置のズレ込み）。
	void ScrollDragTo(const FString& Id, float LogicalY);
	TOptional<FString> ScrollDragId;   // つまみを掴んでいる ScrollView（空=掴んでいない）
	float ScrollGrabDY = 0.f;          // つまみの上端から、掴んだ点までの距離（論理px）
public:
	// 検証の口（合成クリックは Slate に届かないので、押す→動かす→離すを直に通す）。
	// 動かせたら true。回帰は probe-scroll の数値で見る（設計 09/20/05 §2.5）。
	bool ScrollDragProbe(const FString& Id, float FromY, float ToY);
	// `id=<id> off=<px> max=<px> thumb=<y>,<h>` を1行で。id 省略で最初の ScrollView。
	FString DescribeScroll(const FString& Id = FString()) const;
private:

	TArray<KaedeUI::FKaedeFocusNav> FocusNav;   // Hits と同 index
	int32 FocusedIndex = -1;
	// 描き直すたびに焦点を整える（仕様 §7）。前回の鍵と最前面の群から、Modal の開閉・部品の消失・並べ替えを扱う
	void ReconcileFocus(const FString& PrevKey, int32 PrevTop);
	int32 FindFocusKey(const FString& Key, int32 Group) const;   // 一つだけなら番号、無ければ -1、複数（曖昧）なら -2
	int32 LastTopGroup = 0;          // 前回 SetContent したときの最前面の群
	TArray<FString> FocusStack;      // Modal を開く直前の焦点の鍵（閉じたら戻す）
	bool  bFocusVisible = false;  // ゲームパッド/キーボード操作中のみリング表示
	bool  bHoverMovesFocus = false;   // SetHoverMovesFocus

	// ===== 対話フィードバックの状態（設計 07/21/04 §C1）=====
	// 「どのボタンか」＝action（識別子）だけを持つ。番号・矩形は持たない。UIはクリックの
	// たびにヒット配列と描画コマンドを作り直すため、番号や矩形は別のボタンを指し得る。
	FString PressedAction;                 // 押下中（空=なし）
	FString HoverAction;                   // ホバー中（空=なし）
	double HoverStartTime = 0.;            // ホバーフェード（120ms）の起点
	FString FlashAction;                   // クリック成立フラッシュ（150ms）の対象
	double FlashStartTime = -1.0;

	// TextInput 編集状態（id 別バッファ・編集中フィールド）。SetContent を跨いで保持。
	TMap<FString, FString> TextBuffers;
	FString ActiveTextField;                  // 編集中フィールドid（空=無し）
	FString TextFieldAt(FVector2f L) const;   // 論理座標を含む TextField の id（無=空）
	// 指定 id の TextField の論理矩形（スクロール量込み）。見つからなければ false。
	// OutFontPt: その欄の文字の大きさ（論理px。木の fontSize、無ければキャンバス既定）。編集中の SEditableText と
	// 非編集時の描画が同じ値を見る＝クリックして編集に入っても文字の大きさが変わらない。
	bool TextFieldRect(const FString& Id, FVector2f& OutPos, FVector2f& OutSize, float* OutFontPt = nullptr) const;
	// ActiveTextField の変更に合わせて編集ウィジェットの中身とフォーカスを合わせる。
	void SyncTextEditor(bool bTakeFocus);
	void LeaveTextEditor();   // 編集中の欄があれば降りる（ActiveTextField を空にして SyncTextEditor）
	// Enter / Escape の確定。**action を持つ欄は畳まない**（IME の入力コンテキストが
	// 降りたまま戻らなくなる・設計 09/20/05 §1.3）。bEnter=false は取消（必ず畳む）。
	void CommitTextField(bool bEnter);
	// 編集中の欄が操作できなくなっていたら降ろす（画面から消えた・Modal の背面になった・押せなくなった・ScrollView で隠れた）。
	// SetContent とスクロールのたびに呼ぶ（2026-10-04。それまでは消えたときだけ＝PruneActiveTextField）
	void ReconcileTextEditor();
	// 編集器の文字が変わった（SEditableText の OnTextChanged と TypeIntoActiveTextInput が同じ関数を通る）
	void OnEditorTextChanged(const FText& T);
	// 指定 id の TextField が ScrollView の中なら、その見える範囲 (x,y,w,h) を返して true
	bool TextFieldClip(const FString& Id, FVector4f& OutVp) const;
	// ---- 画面ごとの状態（SetScreenId）----
	struct FScreenState
	{
		TMap<FString, FString> TextBuffers;
		TMap<FString, float> ScrollOffsets;
		TArray<FString> FocusStack;
		FString FocusKey;      // その画面を離れたときの焦点の鍵（戻ったとき同じ部品へ）
		int32 LastTop = 0;
	};
	TMap<FString, FScreenState> Screens;   // いまの画面以外の状態（いまの画面のぶんは下の TextBuffers 等そのもの）
	FString ScreenId;                      // いまの画面の名前（空 = 名前の無い 1 つの画面）
	TOptional<FString> PendingFocusKey;    // 画面を切り替えた直後の SetContent が「前回の鍵」として使う（その画面を離れたときの鍵）
public:
	bool IsTextEditorFocused() const;   // 編集ウィジェットがキーボードフォーカスを持っているか（probe-input）
	// 検証の口。Enter の確定と**同じ関数**を通す（caseui:submit を直に叩く経路は
	// OnTextCommitted を通らないので、畳む/畳まないの穴を踏めない）。
	void CommitActiveTextInput() { CommitTextField(/*bEnter=*/true); }
private:
	TSharedPtr<class SEditableText> TextEditor;   // ChildSlot 実体（編集中のみ表示）

	// ---- 文字の選択（2026-10-07）。状態と規則は Private/KaedeTextSelection.h、ここからの繋ぎは KaedeTextSelection.cpp ----
	// 行と器は SetContent が Cmds から集める
	TUniquePtr<KaedeUI::FKaedeTextSelection> Sel;
	KaedeUI::FKaedeSelEnv SelEnv() const;   // 測り方（描くのと同じ書体・大きさ）とスクロール量
	void CollectSelectables();
	// 論理座標の点を含む、いま選べる器（最前面の群・見える範囲の中・押せる当たりの上ではない）。無ければ空
	FString SelectableGroupAt(FVector2f L) const;
	void VerifySelection();            // §4.4（描き直しで行の文字が変わったら消す）
	void SelExtendTo(FVector2f L);
	void SelFinish(FVector2f L);       // 離した: 文を作ってクリップボードへ
	void SelClickThrough(FVector2f L); // 4px 動かずに離した: 従来どおりのクリック

	TArray<KaedeUI::FKaedeDrawCmd> Cmds;
	// 消えていく途中の部品（anim.exit）の、前に描いた描画命令（2026-10-12）
	TArray<KaedeUI::FKaedeDrawCmd> Ghosts;
	TArray<KaedeUI::FKaedeHit> Hits;
	TArray<KaedeUI::FKaedeCanvasSprite> Sprites;
	TOptional<KaedeUI::FKaedeHit> DragHit;  // Slider ドラッグ中のヒット矩形
	// 押下中のボタンヒット（値保持）。発火判定（離し位置との照合）専用で、描画には使わない。
	// 押下中の定期Rebuildで Hits が入れ替わっても取りこぼさないための外置き（07/21/02 B1）。
	TOptional<KaedeUI::FKaedeHit> PressedHit;
	FOnAction OnAction;
	FOnBackgroundClick OnBackgroundClick;
	FLinearColor BgColor = FLinearColor::Black;
	kaedevn::ui::UiMotionState* MotionState = nullptr;   // UIモーション（非所有。SetMotionState）
	FFocusStyle FocusStyle;
	bool bPixelArt = false;
	int32 ScreenCmdNum = -1;            // 画面遷移エンターの適用範囲（SetScreenCmdNum）
};

// ===== 画面を出す入口（利用者向け・2026-10-03）=====
// 画面 JSON を使う側（host）が毎回書いていた「字の計測」と「差し込み → 配置 → キャンバスへ」を 1 つにした。
// ブラウザの MomijiUICanvas.present と同じ形。仕様 Docs/Guides/Working/momiji-ui-framework.md §6、使い方 Docs/Guides/Manuals/momiji-ui-manual.md。
namespace KaedeUI
{
	// 描くときに渡す値（仕様 §6）。Strings は {鍵}、Vars は条件式と bindVar の部品、Lists は repeat の一覧
	struct FKaedeScreenValues
	{
		std::unordered_map<std::string, std::string> Strings;
		std::unordered_map<std::string, double> Vars;
		std::unordered_map<std::string, std::vector<kaedevn::ui::ItemScope>> Lists;
	};

	// 画面 JSON の "bind"（{strings, vars, lists}）を値に読む（-KaedeUiShot と照合の自動テストが使う）。
	// 型の合わない値（strings に数・vars に文字列など）は飛ばして警告を出す。例外を投げない
	MOMIJIUISLATE_API FKaedeScreenValues ReadScreenValues(const nlohmann::json& Bind);

	// Slate の字の計測（同梱の BIZ UDP ゴシック）。fontSize 0 の部品は DefaultPx で測る。描画と同じ測り方
	MOMIJIUISLATE_API FKaedeMeasureFn MakeSlateMeasure(int32 DefaultPx = KaedeLogicalFontSize,
	                                             EKaedeFontFamily Family = EKaedeFontFamily::Gothic);

	// Tree（元の木。書き換えない）の写しへ Values を差し込み、条件で刈り、配置してキャンバスへ渡す。
	// 状態が変わったら（action を受けたら）もう一度呼ぶ。押された action は Canvas.SetOnAction で受ける。
	// 動き（anim）を使う画面は Motion を渡す（host が持ち、Tick で Motion->update(ミリ秒) を呼ぶ）。
	// ScreenId は画面の名前（変わると画面全体が浮かび、前の画面の動きは捨てられる）
	MOMIJIUISLATE_API void PresentScreen(SKaedeUiCanvas& Canvas, const kaedevn::ui::UiNode& Tree,
	                               const FKaedeScreenValues& Values, const kaedevn::ui::UiTheme& Theme,
	                               FLinearColor Bg = FLinearColor::Transparent,
	                               kaedevn::ui::UiMotionState* Motion = nullptr, const FString& ScreenId = TEXT("screen"));
}
