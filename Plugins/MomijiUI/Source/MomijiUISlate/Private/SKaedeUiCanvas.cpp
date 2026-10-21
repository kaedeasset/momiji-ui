// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SKaedeUiCanvas.cpp — 描画コマンド列を Slate へ出す共有キャンバス＋入力処理。
//
// ComputeDrawList（KaedeUiPainter）の出力を受け取って描くだけでなく、
// スクロール・TextInput 編集・フォーカス移動といった「対話」もここが持つ。
// 論理座標はアロット領域へアスペクト維持で収める（レターボックス・LetterboxFor）。
//
// 仕事ごとのファイル（2026-10-18 Issue #73 に分けた）:
//   KaedeUiFonts.cpp            … 書体の解決（MakeKaedeUiFont）と字の計測
//   KaedeUiBrushes.cpp          … 角丸ブラシの控えと PNG → ブラシ
//   KaedeUiCanvasDraw.cpp       … OnPaint の部品（描き先・重ね順の段・合成・Kind ごとの描き方）
//   KaedeTextSelection.cpp      … 文字の選択とコピー
//   SKaedeUiCanvasTextInput.cpp … 文字欄（SEditableText を重ねる）
//   このファイル                … 中身の差し替え・画面ごとの状態・スクロール・焦点・画像の読み込み・マウス・OnPaint の流れ・画面を出す入口
//
// 論理サイズの既定は 1280x720（KaedeLogicalW/H）だが、これは**基準の論理サイズ**
// であってVNの既定ではない。VN は KaedeVnLogicalW/H(1920x1080)、
// 放置錬金は 1920x1080 を明示的に渡す。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "SKaedeUiCanvas.h"
#include "KaedeUiCanvasDraw.h"
#include "KaedeTextSelection.h"

#include "ui/Tween.hpp"   // UiMotionState（UIモーションの毎フレーム合成。設計 07/21/01）
#include "MomijiUICore.h"   // BindScreenTree / ApplyEnabledTree / PruneScreenTree（PresentScreen）
#include <nlohmann/json.hpp>

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SEditableText.h"   // TextEditor（ChildSlot の実体）

DEFINE_LOG_CATEGORY_STATIC(LogKaedeUiCanvas, Log, All);

SKaedeUiCanvas::SKaedeUiCanvas() : Sel(MakeUnique<KaedeUI::FKaedeTextSelection>()) {}
SKaedeUiCanvas::~SKaedeUiCanvas() = default;

void SKaedeUiCanvas::SetContent(TArray<KaedeUI::FKaedeDrawCmd> InCmds, TArray<KaedeUI::FKaedeHit> InHits, FLinearColor InBg)
{
	// フォーカス中の部品の鍵を覚えておき、再構築後も同じ部品へ復元する（Rebuild で index がずれるため。
	// 2026-10-03: action ではなく鍵＝id があれば id。同じ action の別の部品へ移らない）。
	// 画面を切り替えた直後は、その画面を離れたときの鍵（SetScreenId が控えた）を使う＝別の画面の焦点が混ざらない
	const FString PrevFocusKey = PendingFocusKey.IsSet() ? PendingFocusKey.GetValue() : FocusedKey();
	PendingFocusKey.Reset();

	// 消えていく途中の部品（anim.exit・2026-10-12）: 前に描いた描画命令を残し、sample を掛けて描き続ける
	if (MotionState)
	{
		const std::vector<std::string> Exiting = MotionState->exitingKeys();
		TSet<FString> Keys;
		for (const std::string& K : Exiting) Keys.Add(UTF8_TO_TCHAR(K.c_str()));
		Ghosts.RemoveAll([&Keys](const KaedeUI::FKaedeDrawCmd& G) { return !Keys.Contains(G.MotionKey); });
		for (const FString& K : Keys)
		{
			if (Ghosts.ContainsByPredicate([&K](const KaedeUI::FKaedeDrawCmd& G) { return G.MotionKey == K; })) continue;
			for (const KaedeUI::FKaedeDrawCmd& Old : Cmds)
				if (Old.MotionKey == K && Old.Kind != KaedeUI::EKaedeDrawKind::ClipPush && Old.Kind != KaedeUI::EKaedeDrawKind::ClipPop)
				{
					KaedeUI::FKaedeDrawCmd G = Old;
					G.HitIndex = -1;
					Ghosts.Add(G);
				}
		}
	}
	else Ghosts.Reset();
	Cmds = MoveTemp(InCmds);
	Hits = MoveTemp(InHits);
	BgColor = InBg;
	// 押下/ホバー/フラッシュは action で保持しているため、ここで触るものは何も無い
	//（設計 07/21/04 §C1/C2。番号の復元スキャンは廃止＝ズレる余地が無い。ホバーに
	//  触らないので 0.25 秒周期の定期 Rebuild でもフェードがリセットされない）。
	FocusNav = KaedeUI::BuildFocusNav(Hits);   // 幾何から Focus Graph 自動生成（純関数）

	// 焦点を整える（同じ鍵の部品へ戻す・Modal の開閉を控えて戻す・無ければ最前面の先頭へ）
	ReconcileFocus(PrevFocusKey, LastTopGroup);
	LastTopGroup = TopFocusGroup();

	CollectScrollRegions();   // ClipPush から ScrollView 領域を集め、保持スクロール量をクランプ
	ReconcileTextEditor();    // 操作できなくなった編集欄は降ろす（消えた・Modal の背面・押せない・隠れた）
	CollectSelectables();     // 選べる行と器（文字の選択・2026-10-07）
	VerifySelection();        // 選んでいた行の文字が変わった・無くなったら選択を消す（§4.4）
	LoadImageBrushes();       // 描く画像はここで読む（OnPaint はファイルを読まない）
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::SetScrollOffset(const FString& Id, float Px)
{
	ScrollOffsets.Add(Id, FMath::Max(0.f, Px));
	OnScrollChanged();
}

void SKaedeUiCanvas::OnScrollChanged()
{
	ReconcileTextEditor();   // 編集中の欄がスクロールで完全に隠れたら降りる（F2・AC04）
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ===== 画面ごとの状態（2026-10-04・仕様 §6「画面の状態」）=====
void SKaedeUiCanvas::SetScreenId(const FString& Id)
{
	if (Id == ScreenId) return;
	ClearSelection();   // 選択は画面をまたがない（§4.4）
	Sel->Press.Reset();
	// いまの画面の状態を控える（移動なので中身は 1 つ）
	FScreenState& Old = Screens.FindOrAdd(ScreenId);
	Old.TextBuffers = MoveTemp(TextBuffers);
	Old.ScrollOffsets = MoveTemp(ScrollOffsets);
	Old.FocusStack = MoveTemp(FocusStack);
	Old.FocusKey = FocusedKey();
	Old.LastTop = LastTopGroup;
	// 新しい画面の状態へ（無ければ空＝初期値）
	ScreenId = Id;
	FScreenState& New = Screens.FindOrAdd(Id);
	TextBuffers = MoveTemp(New.TextBuffers);
	ScrollOffsets = MoveTemp(New.ScrollOffsets);
	FocusStack = MoveTemp(New.FocusStack);
	LastTopGroup = New.LastTop;
	PendingFocusKey = New.FocusKey;
	Screens.Remove(Id);
	FocusedIndex = -1;
	// 編集中の欄は前の画面のもの。降りる（次の SetContent で新しい画面の欄が操作できれば host が付け直す）
	LeaveTextEditor();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::ResetScreenState(const FString& Id)
{
	if (Id != ScreenId) { Screens.Remove(Id); return; }
	ClearSelection();
	Sel->Press.Reset();
	TextBuffers.Reset();
	ScrollOffsets.Reset();
	FocusStack.Reset();
	LastTopGroup = 0;
	PendingFocusKey.Reset();
	FocusedIndex = -1;
	LeaveTextEditor();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::ResetAllScreenState()
{
	Screens.Reset();
	ResetScreenState(ScreenId);
}

// Cmds の ClipPush から ScrollView のビューポート/中身高を集め、保持スクロール量を有効域へクランプ。
void SKaedeUiCanvas::CollectScrollRegions()
{
	ScrollViewports.Reset();
	ScrollContentH.Reset();
	for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
	{
		if (C.Kind != KaedeUI::EKaedeDrawKind::ClipPush || C.Sprite.IsEmpty()) continue;
		ScrollViewports.Add(C.Sprite, FVector4f(C.X, C.Y, C.W, C.H));
		ScrollContentH.Add(C.Sprite, C.ContentH);
	}
	// 保持スクロール量を再クランプ（中身が縮んだ/画面が変わった場合に範囲外に残さない）。
	for (auto& Kv : ScrollOffsets)
		Kv.Value = FMath::Clamp(Kv.Value, 0.f, ScrollMaxFor(Kv.Key));
}

float SKaedeUiCanvas::ScrollMaxFor(const FString& Id) const
{
	const float* CH = ScrollContentH.Find(Id);
	const FVector4f* Vp = ScrollViewports.Find(Id);
	if (!CH || !Vp) return 0.f;
	return FMath::Max(0.f, *CH - Vp->W);   // W 成分 = ビューポート高
}

float SKaedeUiCanvas::ScrollOffsetFor(const FString& Id) const
{
	const float* Off = ScrollOffsets.Find(Id);
	return Off ? FMath::Clamp(*Off, 0.f, ScrollMaxFor(Id)) : 0.f;
}

// ===== スクロールバーを掴む（2026-09-20・設計 09/20/05 §2）=====

bool SKaedeUiCanvas::ScrollBarFor(const FString& Id, KaedeUI::FKaedeScrollBar& Out) const
{
	const FVector4f* Vp = ScrollViewports.Find(Id);
	const float* CH = ScrollContentH.Find(Id);
	if (!Vp || !CH) return false;
	// FVector4f の成分は (x, y, 幅, 高さ)。CollectScrollRegions と同じ約束。
	return KaedeUI::ScrollBarGeometry(Vp->X, Vp->Y, Vp->Z, Vp->W, *CH, ScrollOffsetFor(Id), Out);
}

FString SKaedeUiCanvas::ScrollBarAt(FVector2f L, bool* bOutOnThumb) const
{
	if (bOutOnThumb) { *bOutOnThumb = false; }
	for (const auto& Kv : ScrollViewports)
	{
		KaedeUI::FKaedeScrollBar Bar;
		if (!ScrollBarFor(Kv.Key, Bar)) { continue; }
		if (L.X < Bar.HitX || L.X >= Bar.HitX + Bar.HitW) { continue; }
		if (L.Y < Bar.TrackY || L.Y >= Bar.TrackY + Bar.TrackH) { continue; }
		if (bOutOnThumb) { *bOutOnThumb = (L.Y >= Bar.ThumbY && L.Y < Bar.ThumbY + Bar.ThumbH); }
		return Kv.Key;
	}
	return FString();
}

void SKaedeUiCanvas::ScrollDragTo(const FString& Id, float LogicalY)
{
	KaedeUI::FKaedeScrollBar Bar;
	if (!ScrollBarFor(Id, Bar)) return;
	// つまみが動ける幅は (溝の高さ − つまみの高さ)。掴んだ点のズレ（ScrollGrabDY）を引いて比に直す。
	const float Travel = Bar.TrackH - Bar.ThumbH;
	if (Travel <= 0.f) return;
	const float Ratio = FMath::Clamp((LogicalY - ScrollGrabDY - Bar.TrackY) / Travel, 0.f, 1.f);
	// 中身の高さはドラッグ中にも変わりうる（応答が1手増える）ので、毎回 ScrollMaxFor で取り直す。
	const float Next = Ratio * ScrollMaxFor(Id);
	if (Next != ScrollOffsetFor(Id))
	{
		ScrollOffsets.Add(Id, Next);
		OnScrollChanged();
	}
}

bool SKaedeUiCanvas::ScrollDragProbe(const FString& Id, float FromY, float ToY)
{
	KaedeUI::FKaedeScrollBar Bar;
	if (!ScrollBarFor(Id, Bar)) return false;
	// 押した点がつまみの外なら、つまみの中心を掴んだことにする（検証の口を座標合わせで落とさない）。
	const bool bOnThumb = (FromY >= Bar.ThumbY && FromY < Bar.ThumbY + Bar.ThumbH);
	ScrollGrabDY = bOnThumb ? (FromY - Bar.ThumbY) : (Bar.ThumbH * 0.5f);
	ScrollDragId = Id;
	ScrollDragTo(Id, ToY);
	ScrollDragId.Reset();
	return true;
}

FString SKaedeUiCanvas::DescribeScroll(const FString& Id) const
{
	FString Key = Id;
	if (Key.IsEmpty())
	{
		for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
		{
			if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPush && !C.Sprite.IsEmpty()) { Key = C.Sprite; break; }
		}
	}
	if (Key.IsEmpty()) return TEXT("id=(none)");
	KaedeUI::FKaedeScrollBar Bar;
	if (!ScrollBarFor(Key, Bar)) return FString::Printf(TEXT("id=%s off=%d max=0 thumb=none"), *Key, FMath::RoundToInt(ScrollOffsetFor(Key)));
	return FString::Printf(TEXT("id=%s off=%d max=%d thumb=%d,%d"), *Key,
		FMath::RoundToInt(ScrollOffsetFor(Key)), FMath::RoundToInt(Bar.MaxScroll),
		FMath::RoundToInt(Bar.ThumbY), FMath::RoundToInt(Bar.ThumbH));
}

FString SKaedeUiCanvas::ScrollViewAt(FVector2f L) const
{
	for (const auto& Kv : ScrollViewports)
	{
		const FVector4f& V = Kv.Value;   // (x,y,w,h)
		if (L.X >= V.X && L.X < V.X + V.Z && L.Y >= V.Y && L.Y < V.Y + V.W) return Kv.Key;
	}
	return FString();
}

// フォーカス中 hit がスクロールビューの外なら、ビューポート内に収まる最小量だけスクロールする。
void SKaedeUiCanvas::EnsureScrollVisible(int32 HitIndex)
{
	if (!Hits.IsValidIndex(HitIndex)) return;
	const KaedeUI::FKaedeHit& H = Hits[HitIndex];
	if (H.ScrollId.IsEmpty()) return;
	const FVector4f* Vp = ScrollViewports.Find(H.ScrollId);
	if (!Vp) return;
	float Off = ScrollOffsetFor(H.ScrollId);
	const float Top = H.Y - Off;            // 画面上の位置
	const float Bottom = H.Y + H.H - Off;
	if (Top < Vp->Y)                 Off -= (Vp->Y - Top);                 // 上へ隠れている → 下げる
	else if (Bottom > Vp->Y + Vp->W) Off += (Bottom - (Vp->Y + Vp->W));   // 下へ隠れている → 上げる
	ScrollOffsets.Add(H.ScrollId, FMath::Clamp(Off, 0.f, ScrollMaxFor(H.ScrollId)));
	ReconcileTextEditor();
}

FReply SKaedeUiCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f L = ToLogical(MyGeometry, MouseEvent);
	const FString Id = ScrollViewAt(L);
	if (Id.IsEmpty()) return FReply::Unhandled();
	const float Cur = ScrollOffsetFor(Id);
	const float Next = FMath::Clamp(Cur - MouseEvent.GetWheelDelta() * WheelNotch(), 0.f, ScrollMaxFor(Id));
	if (Next != Cur) { ScrollOffsets.Add(Id, Next); OnScrollChanged(); }
	return FReply::Handled();
}

void SKaedeUiCanvas::ClickAtProbe(FVector2f L)
{
	const FGeometry Geo = FGeometry::MakeRoot(FVector2f((float)LogicalW, (float)LogicalH), FSlateLayoutTransform());
	const FPointerEvent Ev(0, L, L, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	OnMouseButtonDown(Geo, Ev);
	OnMouseButtonUp(Geo, Ev);
}

// ===== 焦点（Phase 2-3。ゲームパッド/キーボード操作の土台）=====

FString SKaedeUiCanvas::DescribeFocus() const
{
	int32 Grp = 0;
	for (const KaedeUI::FKaedeHit& H : Hits) { Grp = FMath::Max(Grp, H.FocusGroup); }
	int32 Start = -1;
	for (int32 i = 0; i < Hits.Num(); ++i) { if (Hits[i].bFocusable && Hits[i].FocusGroup == Grp) { Start = i; break; } }
	if (Start < 0) return TEXT("group=0 focusable=0 reachable=0 unreachable=0");
	const TArray<KaedeUI::FKaedeFocusNav> Nav = KaedeUI::BuildFocusNav(Hits);
	TSet<int32> Seen; TArray<int32> Stack; Stack.Push(Start); Seen.Add(Start);
	while (Stack.Num() > 0)
	{
		const int32 i = Stack.Pop();
		if (!Nav.IsValidIndex(i)) { continue; }
		for (int32 Next : { Nav[i].Up, Nav[i].Down, Nav[i].Left, Nav[i].Right })
		{
			if (Hits.IsValidIndex(Next) && !Seen.Contains(Next)) { Seen.Add(Next); Stack.Push(Next); }
		}
	}
	int32 Focusable = 0, Reached = 0; TArray<FString> Missed;
	for (int32 i = 0; i < Hits.Num(); ++i)
	{
		if (!Hits[i].bFocusable || Hits[i].FocusGroup != Grp || Hits[i].Action.IsEmpty()) { continue; }
		++Focusable;
		if (Seen.Contains(i)) { ++Reached; } else if (Missed.Num() < 5) { Missed.Add(Hits[i].Action); }
	}
	return FString::Printf(TEXT("group=%d focusable=%d reachable=%d unreachable=%d%s%s"), Grp, Focusable, Reached, Focusable - Reached,
		Missed.Num() ? TEXT(" first=") : TEXT(""), *FString::Join(Missed, TEXT(",")));
}

int32 SKaedeUiCanvas::TopFocusGroup() const
{
	int32 G = 0;
	for (const KaedeUI::FKaedeHit& H : Hits) if (H.FocusGroup > G) G = H.FocusGroup;
	return G;
}

void SKaedeUiCanvas::ResetFocusToFirst()
{
	const int32 Top = TopFocusGroup();
	FocusedIndex = -1;
	for (int32 i = 0; i < Hits.Num(); ++i)
		if (Hits[i].bFocusable && Hits[i].FocusGroup == Top) { FocusedIndex = i; break; }
}

void SKaedeUiCanvas::SetFocusedIndex(int32 Index)
{
	FocusedIndex = (Hits.IsValidIndex(Index) && Hits[Index].bFocusable) ? Index : FocusedIndex;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FString SKaedeUiCanvas::FocusedAction() const
{
	return Hits.IsValidIndex(FocusedIndex) ? Hits[FocusedIndex].Action : FString();
}

FString SKaedeUiCanvas::FocusedKey() const
{
	return Hits.IsValidIndex(FocusedIndex) ? KaedeUI::HitKey(Hits[FocusedIndex]) : FString();
}

FString SKaedeUiCanvas::HitKeyAt(FVector2f L) const
{
	const int32 I = HitAtLogical(L);
	return Hits.IsValidIndex(I) ? KaedeUI::HitKey(Hits[I]) : FString();
}

int32 SKaedeUiCanvas::FindFocusKey(const FString& Key, int32 Group) const
{
	int32 Found = -1, Count = 0;
	for (int32 i = 0; i < Hits.Num(); ++i)
	{
		if (!Hits[i].bFocusable || Hits[i].FocusGroup != Group || KaedeUI::HitKey(Hits[i]) != Key) continue;
		if (Found < 0) Found = i;
		++Count;
	}
	return Count == 1 ? Found : (Count > 1 ? -2 : -1);
}

// 群が増えた（Modal が開いた）→ いまの鍵を控えて最前面の先頭へ／減った（閉じた）→ 控えた鍵へ戻す（無ければ先頭）／
// 同じ → 同じ鍵が最前面に一つだけあれば保持、無い・曖昧なら先頭。ブラウザの reconcileFocus と同じ規則（仕様 §7）
void SKaedeUiCanvas::ReconcileFocus(const FString& PrevKey, int32 PrevTop)
{
	const int32 Top = TopFocusGroup();
	FocusedIndex = -1;
	if (Top > PrevTop)
	{
		if (!PrevKey.IsEmpty()) FocusStack.Push(PrevKey);
		ResetFocusToFirst();
	}
	else if (Top < PrevTop)
	{
		const FString Back = FocusStack.Num() ? FocusStack.Pop() : FString();
		const int32 I = Back.IsEmpty() ? -1 : FindFocusKey(Back, Top);
		if (I >= 0) FocusedIndex = I; else ResetFocusToFirst();
	}
	else
	{
		const int32 I = PrevKey.IsEmpty() ? -1 : FindFocusKey(PrevKey, Top);
		if (I >= 0) FocusedIndex = I; else ResetFocusToFirst();
	}
}

bool SKaedeUiCanvas::MoveFocus(int32 dx, int32 dy)
{
	// 焦点が無い・最前面の群に無い（Modal が開いた直後など）→ 最前面の先頭へ寄せる
	if (!Hits.IsValidIndex(FocusedIndex) || Hits[FocusedIndex].FocusGroup != TopFocusGroup())
	{
		ResetFocusToFirst();
		EnsureScrollVisible(FocusedIndex);
		Invalidate(EInvalidateWidgetReason::Paint);
		return Hits.IsValidIndex(FocusedIndex);
	}
	if (!FocusNav.IsValidIndex(FocusedIndex)) return false;
	const KaedeUI::FKaedeFocusNav& N = FocusNav[FocusedIndex];
	int32 Next = -1;
	if (dy < 0) Next = N.Up; else if (dy > 0) Next = N.Down;
	else if (dx < 0) Next = N.Left; else if (dx > 0) Next = N.Right;
	if (Hits.IsValidIndex(Next))
	{
		FocusedIndex = Next;
		bFocusVisible = true;   // 方向入力が来た=リング表示
		EnsureScrollVisible(FocusedIndex);   // スクロールビュー配下なら見える位置へ寄せる
		Invalidate(EInvalidateWidgetReason::Paint);
		return true;
	}
	return false;
}

int32 SKaedeUiCanvas::HitAtLogical(FVector2f L) const
{
	for (int32 i = Hits.Num() - 1; i >= 0; --i)  // 最前面（後勝ち）
	{
		const KaedeUI::FKaedeHit& H = Hits[i];
		float hy = H.Y;
		if (!H.ScrollId.IsEmpty())
		{
			// スクロールビュー配下: 量ぶんずらし、ビューポート外（クリップされる範囲）は無効。
			hy -= ScrollOffsetFor(H.ScrollId);
			if (const FVector4f* Vp = ScrollViewports.Find(H.ScrollId))
				if (L.Y < Vp->Y || L.Y >= Vp->Y + Vp->W) continue;
		}
		if (L.X >= H.X && L.X < H.X + H.W && L.Y >= hy && L.Y < hy + H.H) return i;
	}
	return -1;
}

// ===== 画像（sprite パス → ブラシ・Phase 3）=====
// プロジェクト相対は ProjectDir を前置して絶対化。一度解決した結果をキャッシュし、毎フレームの再読込を防ぐ。
// 失敗は2秒だけ負ヒットとしてキャッシュし、その後の読み込み（SetContent など）で再試行する（開発中にPNGを後から置いた/
// 差し替えた場合に同一セッションで復帰する。Codexレビュー Docs/CodexReports/2026/07/14/01 §注意点）。
// 読むのは描く前（LoadImageBrushes）だけで、OnPaint は控えを引く（ResolveImageBrush）。
void SKaedeUiCanvas::SetDynamicBrush(const FString& Key, TSharedPtr<FSlateBrush> Brush)
{
	if (Brush.IsValid()) DynamicBrushes.Add(Key, Brush);
	else                 DynamicBrushes.Remove(Key);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::SetPixelArt(bool b)
{
	if (bPixelArt == b) return;
	bPixelArt = b;
	LoadImageBrushes();   // ドット絵は別のテクスチャ（最近傍）
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::SetFocusStyle(const FFocusStyle& S)
{
	FocusStyle = S;
	if (!FocusStyle.Cursor.IsEmpty()) LoadImageBrush(FocusStyle.Cursor);
	Invalidate(EInvalidateWidgetReason::Paint);
}

FString SKaedeUiCanvas::ImageCacheKey(const FString& Sprite) const
{
	return bPixelArt ? Sprite + TEXT("#nearest") : Sprite;   // ドット絵は別のテクスチャ（2026-10-12）
}

void SKaedeUiCanvas::LoadImageBrushes()
{
	for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
		if (C.Kind == KaedeUI::EKaedeDrawKind::Image) LoadImageBrush(C.Sprite);
	for (const KaedeUI::FKaedeDrawCmd& C : Ghosts)
		if (C.Kind == KaedeUI::EKaedeDrawKind::Image) LoadImageBrush(C.Sprite);
	if (!FocusStyle.Cursor.IsEmpty()) LoadImageBrush(FocusStyle.Cursor);
}

void SKaedeUiCanvas::LoadImageBrush(const FString& Sprite)
{
	// 実行時生成のブラシが最優先（ファイル読込へ落とさない）。
	if (DynamicBrushes.Contains(Sprite)) return;
	const FString CacheKey = ImageCacheKey(Sprite);
	if (const TSharedPtr<FSlateBrush>* Found = ImageBrushCache.Find(CacheKey))
	{
		if (Found->IsValid()) return;
		const double* FailAt = ImageBrushFailAt.Find(CacheKey);
		if (FailAt && FPlatformTime::Seconds() - *FailAt < 2.0) return;   // 再試行はまだ
		ImageBrushCache.Remove(CacheKey);   // 期限切れ → 下の読込へ
	}

	FString Path = Sprite;
	if (FPaths::IsRelative(Path))
		Path = FPaths::ProjectDir() / Path;

	UTexture2D* Tex = nullptr;
	TSharedPtr<FSlateBrush> Brush = KaedeUI::KaedeLoadPngBrush(Path, Tex, bPixelArt);
	if (Brush.IsValid() && Tex)
	{
		ImageTextures.Add(TStrongObjectPtr<UTexture2D>(Tex));  // GC 保護
		ImageBrushFailAt.Remove(CacheKey);
	}
	else
	{
		ImageBrushFailAt.Add(CacheKey, FPlatformTime::Seconds());
	}
	ImageBrushCache.Add(CacheKey, Brush);
}

const FSlateBrush* SKaedeUiCanvas::ResolveImageBrush(const FString& Sprite) const
{
	if (const TSharedPtr<FSlateBrush>* Dyn = DynamicBrushes.Find(Sprite)) return Dyn->Get();
	const TSharedPtr<FSlateBrush>* Found = ImageBrushCache.Find(ImageCacheKey(Sprite));
	return Found ? Found->Get() : nullptr;
}

void SKaedeUiCanvas::SetSprites(TArray<KaedeUI::FKaedeCanvasSprite> InSprites)
{
	Sprites = MoveTemp(InSprites);
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ===== マウス =====

FReply SKaedeUiCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2f L = ToLogical(MyGeometry, MouseEvent);

	// スクロールバー（2026-09-20）。**どの判定より先に見る**——バーは板の上に描かれており、
	// 触れる幅は子が空けている 10px なので、下の要素と取り合いにならない。
	{
		bool bOnThumb = false;
		const FString BarId = ScrollBarAt(L, &bOnThumb);
		if (!BarId.IsEmpty())
		{
			KaedeUI::FKaedeScrollBar Bar;
			ScrollBarFor(BarId, Bar);
			if (bOnThumb)
			{
				ScrollGrabDY = L.Y - Bar.ThumbY;
				ScrollDragId = BarId;
				Invalidate(EInvalidateWidgetReason::Paint);
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
			// 溝の空いている所は1画面送り。送る量はホイール1ノッチを残す＝
			// 送った先に見覚えのある行が1つ残るので、長い応答ログでも位置を見失わない。
			const float Notch = WheelNotch();
			const float Page = FMath::Max(Notch, Bar.TrackH - Notch);
			const float Cur = ScrollOffsetFor(BarId);
			const float Next = FMath::Clamp(L.Y < Bar.ThumbY ? Cur - Page : Cur + Page, 0.f, ScrollMaxFor(BarId));
			if (Next != Cur) { ScrollOffsets.Add(BarId, Next); OnScrollChanged(); }
			return FReply::Handled();
		}
	}

	// TextInput をクリックしたら編集フォーカスを取る（OnAction は発火しない）。外側クリックは編集解除。
	const FString ClickedField = TextFieldAt(L);
	if (!ClickedField.IsEmpty())
	{
		ClearSelection();   // 器の外を押した（§4.4）
		ActiveTextField = ClickedField;
		bFocusVisible = false;
		// フォーカスは自分ではなく SEditableText へ渡す。ここを SharedThis(this) にすると
		// キー入力がキャンバスに来てしまい、IME が働かない（この修正の要点）。
		SyncTextEditor(/*bTakeFocus=*/true);
		return FReply::Handled().SetUserFocus(TextEditor.ToSharedRef(), EFocusCause::Mouse);
	}
	// 文字の選択（2026-10-07・設計 10/07/01 §4.3）: 選べる器の中で、押せる当たりの上でなければ、押した点を控える（まだ選択ではない）。
	// 4px 動けば選択になり、動かずに離せば従来どおりのクリック（SelClickThrough）。編集中の欄はこの時点では降ろさない
	//（選んで離したあと、そのまま入力欄へ貼れるように。クリックで終われば従来どおり降りる）
	{
		const FString Group = SelectableGroupAt(L);
		if (!Group.IsEmpty())
		{
			KaedeUI::FKaedeTextSelection::FPress P;
			P.Group = Group;
			P.P = L;
			Sel->Press = P;
			bFocusVisible = false;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	ClearSelection();   // 器の外・当たりを押した（§4.4）

	LeaveTextEditor();

	const int32 Idx = HitAtLogical(L);
	if (Idx < 0)
	{
		// どのUIヒット領域にも当たらなかった = 背景（＝3D工房）へのクリック。
		// ホストが受けて世界へレイを飛ばす（錬金釜クリック）。未設定/未消費なら従来どおり
		// Unhandled にして下のウィジェット/ビューポートへ流す。
		if (OnBackgroundClick && OnBackgroundClick(FVector2D(L.X, L.Y))) return FReply::Handled();
		return FReply::Unhandled();
	}

	// 値コピー必須: OnAction → Rebuild → SetContent が Hits を丸ごと差し替えるため、
	// 参照で持つと OnAction から戻った時点で解放済みになる。
	const KaedeUI::FKaedeHit H = Hits[Idx];
	if (!bHoverMovesFocus) bFocusVisible = false;   // マウス操作 → リング非表示（Hover 優先）。焦点がマウスに付いてくる画面は印を出したまま
	if (Hits[Idx].bFocusable) FocusedIndex = Idx;
	// 押下表示は action に紐づける（設計 07/21/04 §C2）。action 空の Modal backdrop は
	// 押下扱いにしない＝背景を押しても全画面が暗転しない。
	PressedAction = H.Action;
	Invalidate(EInvalidateWidgetReason::Paint);
	if (H.Action.IsEmpty()) return FReply::Handled();  // Modal backdrop 等の吸収
	const float RelX = H.W > 0.f ? FMath::Clamp((L.X - H.X) / H.W, 0.f, 1.f) : 0.f;
	if (KaedeUI::IsSliderAction(H.Action))
	{
		// Slider は従来どおり押下で即反映＋ドラッグ（値の追従が本体のため）。
		if (OnAction) OnAction(H.Action, RelX);
		DragHit = H;  // ドラッグ継続用にコピー（Rebuild で Hits が入れ替わっても安全）
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	// ボタン類は標準どおり「離した瞬間」に発火（07/21/02 B1）。押している間は
	// 0.96縮小＋暗転が見える。押下ヒットは値で保持し、途中の定期Rebuild（0.25s）で
	// Hits が入れ替わっても取りこぼさない（発火判定は離し位置と action の照合）。
	PressedHit = H;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SKaedeUiCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f L = ToLogical(MyGeometry, MouseEvent);
	// スクロールバーを掴んでいる間は、それだけを見る（ホバーの更新もしない＝板の上を
	// 横切っても下のボタンが光らない）。
	if (ScrollDragId.IsSet() && HasMouseCapture())
	{
		ScrollDragTo(ScrollDragId.GetValue(), L.Y);
		return FReply::Handled();
	}
	// 文字の選択のドラッグ中は、それだけを見る（ホバーも動かさない）
	if (Sel->Press.IsSet())
	{
		const KaedeUI::FKaedeTextSelection::FPress& P = Sel->Press.GetValue();
		if (P.bDead) return FReply::Handled();
		if (!P.bDragging)
		{
			if (FVector2f::Distance(P.P, L) < 4.f) return FReply::Handled();
			Sel->Begin(SelEnv());
		}
		SelExtendTo(L);
		return FReply::Handled();
	}
	// Hover 更新（マウス操作中はリングを消し Hover 表示。入力設計方針 §6）。
	// 対象は action で持つ（番号だと Rebuild でズレて別ボタンが光る。設計 07/21/04 §C2）。
	const int32 NewHover = HitAtLogical(L);
	if (bHoverMovesFocus)
	{
		// 乗せた項目に焦点（選んでいる印）を移す。項目の無い所では動かさない（ふつうの RPG と同じ）
		if (Hits.IsValidIndex(NewHover) && Hits[NewHover].bFocusable && (NewHover != FocusedIndex || !bFocusVisible))
		{
			SetFocusedIndex(NewHover);
			bFocusVisible = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		return FReply::Unhandled();
	}
	const FString NewHoverAction = (Hits.IsValidIndex(NewHover) && Hits[NewHover].bFocusable)
		? Hits[NewHover].Action : FString();
	if (NewHoverAction != HoverAction || bFocusVisible)
	{
		// ホバーフェードイン（B2・07/21/02）の起点時刻。対象が変わるたびに 0 からフェード。
		if (NewHoverAction != HoverAction) HoverStartTime = FPlatformTime::Seconds();
		HoverAction = NewHoverAction;
		bFocusVisible = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (!DragHit.IsSet() || !HasMouseCapture()) return FReply::Unhandled();
	const KaedeUI::FKaedeHit& H = DragHit.GetValue();
	const float RelX = H.W > 0.f ? FMath::Clamp((L.X - H.X) / H.W, 0.f, 1.f) : 0.f;
	if (OnAction) OnAction(H.Action, RelX);
	return FReply::Handled();
}

FReply SKaedeUiCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate は素早い2回目の押下をダブルクリックとして送る。押下として扱わないと連打の
	// 2回目が丸ごと消える（1回おきに無反応）。連打は主ユースケース（設計 07/21/04 §C2）。
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

void SKaedeUiCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Alt+Tab 等でキャプチャが外れると Up が来ない。押下表示が固着しないよう落とす。
	Sel->Press.Reset();   // 選択のドラッグは途中で終わる（選んだ範囲は残す・コピーはしない）
	if (PressedHit.IsSet() || !PressedAction.IsEmpty() || DragHit.IsSet() || ScrollDragId.IsSet())
	{
		PressedHit.Reset();
		PressedAction.Reset();
		DragHit.Reset();
		ScrollDragId.Reset();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

FReply SKaedeUiCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!PressedAction.IsEmpty()) { PressedAction.Reset(); Invalidate(EInvalidateWidgetReason::Paint); }
	if (Sel->Press.IsSet() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const KaedeUI::FKaedeTextSelection::FPress P = Sel->Press.GetValue();
		Sel->Press.Reset();
		const FVector2f L = ToLogical(MyGeometry, MouseEvent);
		if (P.bDead) {}
		else if (P.bDragging) { SelFinish(L); }
		else { SelClickThrough(L); }
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (ScrollDragId.IsSet() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ScrollDragId.Reset();
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (DragHit.IsSet() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		DragHit.Reset();
		return FReply::Handled().ReleaseMouseCapture();
	}
	// ボタンの発火（押下開始したヒットの上で離したときだけ。外して離せばキャンセル＝標準動作）。
	if (PressedHit.IsSet() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const KaedeUI::FKaedeHit Pressed = PressedHit.GetValue();
		PressedHit.Reset();
		const FVector2f L = ToLogical(MyGeometry, MouseEvent);
		// 離し位置の最新ヒットと action で照合（Rebuild で index がずれても正しく判定）。
		const int32 UpIdx = HitAtLogical(L);
		if (UpIdx >= 0 && Hits.IsValidIndex(UpIdx) && Hits[UpIdx].Action == Pressed.Action)
		{
			// クリック成立の可視フィードバック（07/21: 押下縮小だけでは速いクリックで
			// 見えない）。対象は action で控える＝そのボタン自身の描画に 150ms 合成される。
			// 発火でレイアウトが変わりボタンが消えるクリックは、画面遷移エンター（B3）が
			// 反応を担う（設計 07/21/04 §C4。「消えていたら描かない」判断は書かない）。
			FlashAction = Pressed.Action;
			FlashStartTime = FPlatformTime::Seconds();
			const float RelX = Pressed.W > 0.f
				? FMath::Clamp((L.X - Pressed.X) / Pressed.W, 0.f, 1.f) : 0.f;
			if (OnAction) OnAction(Pressed.Action, RelX);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

// ===== 描画 =====

SKaedeUiCanvas::FLetterbox SKaedeUiCanvas::LetterboxFor(FVector2f Size) const
{
	FLetterbox R;
	R.Scale = FMath::Min(Size.X / (float)LogicalW, Size.Y / (float)LogicalH);
	R.OffX = (Size.X - LogicalW * R.Scale) * 0.5f;
	R.OffY = (Size.Y - LogicalH * R.Scale) * 0.5f;
	return R;
}

float SKaedeUiCanvas::PaintedScale() const
{
	const FVector2f Size = GetPaintSpaceGeometry().GetLocalSize();
	if (Size.X <= 0.f || Size.Y <= 0.f) return 1.f;
	return LetterboxFor(Size).Scale;
}

FVector2f SKaedeUiCanvas::ToLogical(const FGeometry& Geo, const FPointerEvent& Ev) const
{
	const FVector2f Local(Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()));
	const FLetterbox LB = LetterboxFor(Geo.GetLocalSize());
	if (LB.Scale <= 0.f) return FVector2f::ZeroVector;
	return FVector2f((Local.X - LB.OffX) / LB.Scale, (Local.Y - LB.OffY) / LB.Scale);
}

void SKaedeUiCanvas::WalkCmds(bool bWithGhosts, TFunctionRef<bool(int32 Index, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)> Fn) const
{
	const KaedeUI::FKaedeDrawCmd* Clip = nullptr;
	const int32 Total = Cmds.Num() + (bWithGhosts ? Ghosts.Num() : 0);
	for (int32 i = 0; i < Total; ++i)
	{
		const KaedeUI::FKaedeDrawCmd& C = i < Cmds.Num() ? Cmds[i] : Ghosts[i - Cmds.Num()];
		if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPush) Clip = &C;
		const bool bGo = Fn(i, C, Clip);
		if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPop) Clip = nullptr;
		if (!bGo) return;
	}
}

int32 SKaedeUiCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace KaedeUI;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 0.f || Size.Y <= 0.f) return LayerId;
	const FLetterbox LB = LetterboxFor(Size);
	const FKaedePaintCtx P(AllottedGeometry, OutDrawElements, LB.Scale, LB.OffX, LB.OffY, FontSize, FontFamily);
	FKaedeLayerStack Layers(LayerId);

	// 論理領域の下地（テーマ背景色。アルファ0なら背後の3Dレベルが透ける）。
	if (BgColor.A > 0.f)
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, P.Box(0, 0, LogicalW * LB.Scale, LogicalH * LB.Scale), P.White, ESlateDrawEffect::None, BgColor);
	// スプライトレイヤ（2Dキャラ等）: 背景の上・UIコマンドの下。
	for (const FKaedeCanvasSprite& S : Sprites)
	{
		if (!S.Brush.IsValid()) continue;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, P.Box(S.X, S.Y, S.W * LB.Scale, S.H * LB.Scale), S.Brush.Get(), ESlateDrawEffect::None, FLinearColor::White);
	}

	// 画面遷移エンター（B3・07/21/02）: 画面コンテンツ範囲（先頭 ScreenCmdNum 個）へ一律の fade+上昇を掛ける。HUD/トーストは範囲外＝固定。
	kaedevn::ui::UiMotionSample ScreenSE;
	const bool bScreenSE = MotionState && ScreenCmdNum != 0 && MotionState->screenEnterSample(ScreenSE);

	// 命令を 1 つずつ合成して描く。ScrollView はクリップして、閉じるときにスクロールバーを描く
	bool bFocusFillDone = false;
	WalkCmds(/*bWithGhosts=*/true, [&](int32 CmdIdx, const FKaedeDrawCmd& C0, const FKaedeDrawCmd* Clip)
	{
		const float Offset = Clip && !Clip->Sprite.IsEmpty() ? ScrollOffsetFor(Clip->Sprite) : 0.f;
		if (C0.Kind == EKaedeDrawKind::ClipPush)
		{
			P.PushClip(C0.X, C0.Y, C0.W, C0.H);
			return true;
		}
		if (C0.Kind == EKaedeDrawKind::ClipPop)
		{
			if (Clip)
			{
				// 掴んでいる間は明るくする（どれを持っているか分かる）。色はテーマの accent（2026-10-12）
				const bool bDragging = ScrollDragId.IsSet() && ScrollDragId.GetValue() == Clip->Sprite;
				const FLinearColor& R = FocusStyle.Ring;
				DrawKaedeScrollBar(P, Layers.Layer + 1, *Clip, Offset, bDragging ? R : FLinearColor(R.R, R.G, R.B, 0.8f));
			}
			OutDrawElements.PopClip();
			return true;
		}
		float FbMul = 1.f, FbWhite = 0.f;
		const FKaedeDrawCmd C = ComposeCmd(CmdIdx, C0, Offset, bScreenSE ? &ScreenSE : nullptr, P, Layers.Layer, bFocusFillDone, FbMul, FbWhite);
		Layers.Before(C, LB.Scale, FontSize);
		DrawCmd(P, Layers, C, FbMul, FbWhite);
		return true;
	});

	const int32 Layer = Layers.Layer;
	PaintSelection(P, Layer);
	// ===== 状態オーバーレイ（UI コマンドの上）: Focus リングのみ =====
	// Hover / 押下 / クリックフラッシュは ComposeCmd で本人に合成済み（設計 07/21/04 §C3）。
	// 独立オーバーレイに戻さないこと: スクロール未補正・クリップ外描画で「別のボタンが光る」。
	if (bFocusVisible && Hits.IsValidIndex(FocusedIndex) && Hits[FocusedIndex].bFocusable)
	{
		const FSlateBrush* Cursor = FocusStyle.Cursor.IsEmpty() ? nullptr : ResolveImageBrush(FocusStyle.Cursor);
		DrawKaedeFocusRing(P, Layer, Hits[FocusedIndex], FocusStyle, Cursor);
	}
	return PaintTextEditor(Args, P, MyCullingRect, Layer, InWidgetStyle, bParentEnabled);
}

KaedeUI::FKaedeDrawCmd SKaedeUiCanvas::ComposeCmd(int32 CmdIdx, const KaedeUI::FKaedeDrawCmd& C0, float ScrollOffset, const kaedevn::ui::UiMotionSample* ScreenSE,
	const KaedeUI::FKaedePaintCtx& P, int32 Layer, bool& bFocusFillDone, float& OutFbMul, float& OutFbWhite) const
{
	using namespace KaedeUI;
	// sRGB(#rrggbb) → リニア変換は描くときに行う（/255 の直渡しだと全色が明るく浮き、
	// ブラウザの描画器の見た目とずれる）。スクロールビュー配下は Y をスクロール量ぶんずらす。
	FKaedeDrawCmd C = C0;
	if (ScrollOffset != 0.f) ShiftKaedeCmdY(C, -ScrollOffset);

	// ---- 画面遷移エンター（一定の基本反応 B3）----
	if (ScreenSE && (ScreenCmdNum < 0 || CmdIdx < ScreenCmdNum))
	{
		ShiftKaedeCmdY(C, ScreenSE->offY);
		C.Color.A = (uint8)FMath::RoundToInt((float)C.Color.A * ScreenSE->alpha);
		C.Alpha *= ScreenSE->alpha;
	}

	// ---- 対話フィードバック（設計 07/21/04 §C3）----
	// 押下/ホバー/フラッシュは「押した本人」の描画コマンドに直接合成する。スクロールのクリップとシフトは
	// 既に適用済み＝オーバーレイの手動座標補正（別の行が光る/暗転する温床）が要らない。照合は action＝識別子で、番号では引かない。
	OutFbMul = 1.f;     // RGB 乗算（押下の沈み）
	OutFbWhite = 0.f;   // 白へ寄せる比（ホバー/フラッシュ）
	if (C.HitIndex >= 0 && Hits.IsValidIndex(C.HitIndex))
	{
		const FKaedeHit& H = Hits[C.HitIndex];
		if (!PressedAction.IsEmpty() && H.Action == PressedAction)
		{
			// 押している間 0.96 倍（ヒット矩形の中心基準。スクロール量は C と同じだけ引く）。
			ScaleKaedeCmdAbout(C, H.X + H.W * 0.5f, H.Y + H.H * 0.5f - (H.ScrollId.IsEmpty() ? 0.f : ScrollOffset), 0.96f);
			OutFbMul = 0.85f;   // 旧・黒18%オーバーレイの置き換え
		}
		else if (!bFocusVisible && !HoverAction.IsEmpty() && H.Action == HoverAction)
		{
			OutFbWhite = 0.10f * FMath::Clamp((float)((FPlatformTime::Seconds() - HoverStartTime) / 0.12), 0.f, 1.f);
		}
		if (FlashStartTime >= 0.0 && !FlashAction.IsEmpty() && H.Action == FlashAction)
		{
			const float T01 = (float)((FPlatformTime::Seconds() - FlashStartTime) / 0.15);
			if (T01 < 1.f) OutFbWhite = FMath::Max(OutFbWhite, 0.22f * (1.f - T01));
		}
	}
	// 焦点の部品（テーマの select・2026-10-12）: 文字の色・最初の地の色を替える
	if (bFocusVisible && FocusedIndex >= 0 && C.HitIndex == FocusedIndex)
	{
		const FKaedeHit* FH = Hits.IsValidIndex(FocusedIndex) ? &Hits[FocusedIndex] : nullptr;
		const float HitY = FH ? FH->Y - (FH->ScrollId.IsEmpty() ? 0.f : ScrollOffsetFor(FH->ScrollId)) : 0.f;
		bFocusFillDone = ApplyKaedeFocusStyle(FocusStyle, C, bFocusFillDone, FH, HitY, P, Layer);
	}
	BlendKaedeCmdColor(C, OutFbMul, OutFbWhite);

	// ---- UIモーション合成（設計 07/21/01）----
	// MotionKey 付きコマンドだけ毎フレーム sample を引く。ヒット矩形は動かさない。
	// アクティブなトラックが無いキーは sample が false を返し素通り（静止時ゼロコスト）。
	kaedevn::ui::UiMotionSample MS;
	if (MotionState && !C.MotionKey.IsEmpty() && MotionState->sample(TCHAR_TO_UTF8(*C.MotionKey), MS))
		ApplyKaedeMotion(C, MS, ScrollOffset);
	return C;
}

void SKaedeUiCanvas::DrawCmd(const KaedeUI::FKaedePaintCtx& P, KaedeUI::FKaedeLayerStack& Layers, const KaedeUI::FKaedeDrawCmd& C, float FbMul, float FbWhite) const
{
	using namespace KaedeUI;
	const int32 Layer = Layers.Layer;
	switch (C.Kind)
	{
	case EKaedeDrawKind::ClipPush:
	case EKaedeDrawKind::ClipPop:
	case EKaedeDrawKind::SelectArea:   // 文字の選択の器（描かない）
		break;  // クリップは OnPaint が処理済み（switch 網羅のため）
	case EKaedeDrawKind::Rect:        DrawKaedeRect(P, Layer, C); break;
	case EKaedeDrawKind::RectOutline: DrawKaedeRectOutline(P, Layer, C); break;
	case EKaedeDrawKind::Lines:       DrawKaedeLines(P, Layer, C); break;
	case EKaedeDrawKind::Ellipse:     DrawKaedeEllipse(P, Layer, C); break;
	case EKaedeDrawKind::Text:        DrawKaedeText(P, Layer, C); break;
	// 画像も本人なら同じだけ沈む/光る（C.Color を持たないので係数を直接掛ける）
	case EKaedeDrawKind::Image:       DrawKaedeImage(P, Layer, C, ResolveImageBrush(C.Sprite), FbMul + (1.f - FbMul) * FbWhite); break;
	case EKaedeDrawKind::Gradient:    DrawKaedeGradient(P, Layer, C); break;
	case EKaedeDrawKind::Polygon:     DrawKaedePolygon(P, Layer, C); break;
	case EKaedeDrawKind::Blur:        DrawKaedeBlur(P, Layer, C); ++Layers.Layer; break;   // 後ろをぼかしたものの上へ次を積む
	case EKaedeDrawKind::TextField:
	{
		// 編集中のフィールドは ChildSlot の SEditableText が本文もキャレットも描く。
		// ここで描くと二重になる（変換中の文字列が本物側にしか無いのでズレる）。
		if (C.Sprite == ActiveTextField) break;
		const FString* Buf = TextBuffers.Find(C.Sprite);
		DrawKaedeTextField(P, Layer, C, Buf ? *Buf : FString());
		break;
	}
	}
}

// ===== 画面を出す入口（宣言は SKaedeUiCanvas.h の末尾）=====
namespace KaedeUI
{
	FKaedeScreenValues ReadScreenValues(const nlohmann::json& Bind)
	{
		// 型の合わない値は飛ばして警告する（Ticker やテストの中から例外を漏らさない）
		FKaedeScreenValues V;
		if (!Bind.is_object()) return V;
		auto Skip = [](const char* Where, const std::string& Key)
		{
			UE_LOG(LogKaedeUiCanvas, Warning, TEXT("[momiji-ui] bind.%s.%s の型が合わない（飛ばす）"), UTF8_TO_TCHAR(Where), UTF8_TO_TCHAR(Key.c_str()));
		};
		const auto S = Bind.find("strings");
		if (S != Bind.end() && S->is_object())
			for (auto It = S->begin(); It != S->end(); ++It)
			{
				if (It->is_string()) V.Strings[It.key()] = It->get<std::string>();
				else Skip("strings", It.key());
			}
		const auto N = Bind.find("vars");
		if (N != Bind.end() && N->is_object())
			for (auto It = N->begin(); It != N->end(); ++It)
			{
				if (It->is_number()) V.Vars[It.key()] = It->get<double>();
				else Skip("vars", It.key());
			}
		const auto Ls = Bind.find("lists");
		if (Ls != Bind.end() && Ls->is_object())
			for (auto It = Ls->begin(); It != Ls->end(); ++It)
			{
				if (!It->is_array()) { Skip("lists", It.key()); continue; }
				std::vector<kaedevn::ui::ItemScope> L;   // 1 つの一覧の行
				for (const nlohmann::json& E : *It)
				{
					if (!E.is_object()) { Skip("lists", It.key()); continue; }
					kaedevn::ui::ItemScope Row;
					for (auto K = E.begin(); K != E.end(); ++K) Row[K.key()] = K->is_string() ? K->get<std::string>() : K->dump();
					L.push_back(std::move(Row));
				}
				V.Lists[It.key()] = std::move(L);
			}
		return V;
	}

	void PresentScreen(SKaedeUiCanvas& Canvas, const kaedevn::ui::UiNode& Tree, const FKaedeScreenValues& Values,
	                   const kaedevn::ui::UiTheme& Theme, FLinearColor Bg,
	                   kaedevn::ui::UiMotionState* Motion, const FString& ScreenId)
	{
		kaedevn::ui::UiNode Copy = Tree;
		ApplyVarsToTree(Copy, Values.Vars);                                         // 仕様 §6-1
		KaedeCore::BindScreenTree(Copy, Values.Strings, Values.Vars, Values.Lists);  // §6-2・3（と刈り込み）
		KaedeCore::ApplyEnabledTree(Copy, Values.Vars);                             // §6-4
		KaedeCore::PruneScreenTree(Copy, Values.Vars);
		TArray<FKaedeHit> Hits;
		FKaedeUiSkin Skin;
		Skin.Theme = Theme;
		FKaedeMotionCtx MotionCtx;
		if (Motion)
		{
			// 動き: このツリーで見た anim の部品を登録し（beginSync〜endSync）、pulse の whileIf は Values.Vars で評価する
			MotionCtx.State = Motion;
			MotionCtx.ScreenId = ScreenId;
			const std::unordered_map<std::string, double> Vars = Values.Vars;
			MotionCtx.EvalCond = [Vars](const std::string& Expr)
			{
				kaedevn::ui::VarEnv Env;
				Env.resolveNum = kaedevn::ui::numResolver(Vars);
				return kaedevn::ui::evalCondExpr(Expr, Env);
			};
			Motion->beginSync(TCHAR_TO_UTF8(*ScreenId));
		}
		TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Copy, Canvas.GetLogicalW(), Canvas.GetLogicalH(),
			MakeSlateMeasure(Canvas.GetFontSize()), &Hits, Skin, Motion ? &MotionCtx : nullptr);
		if (Motion) { Motion->endSync(); }
		Canvas.SetMotionState(Motion);
		{
			SKaedeUiCanvas::FFocusStyle FS;
			const auto& Se = Theme.select;
			const kaedevn::ui::Color RingC = (Se.on && Se.hasBorder) ? Se.border : Theme.accent;
			FS.Ring = FLinearColor::FromSRGBColor(FColor(RingC.r, RingC.g, RingC.b, RingC.a));
			if (Se.on)
			{
				FS.bText = Se.hasText; FS.Text = FColor(Se.text.r, Se.text.g, Se.text.b, Se.text.a);
				FS.bFill = Se.hasFill || Se.gradient.on; FS.Fill = FColor(Se.fill.r, Se.fill.g, Se.fill.b, Se.fill.a);
				if (Se.gradient.on)
				{
					for (const auto& St : Se.gradient.stops) { FS.GradColors.Add(FColor(St.first.r, St.first.g, St.first.b, St.first.a)); FS.GradPos.Add(St.second); }
					FS.bGradH = Se.gradient.horizontal;
				}
				FS.bBorder = Se.hasBorder; FS.Border = FColor(Se.border.r, Se.border.g, Se.border.b, Se.border.a);
				FS.Cursor = UTF8_TO_TCHAR(Se.cursor.sprite.c_str());
				FS.CursorW = (float)Se.cursor.w; FS.CursorH = (float)Se.cursor.h; FS.CursorDx = (float)Se.cursor.dx; FS.CursorDy = (float)Se.cursor.dy;
			}
			Canvas.SetFocusStyle(FS);
		}
		Canvas.SetScreenId(ScreenId);   // 画面ごとの状態（文字欄・スクロール量・焦点）へ切り替える（仕様 §6「画面の状態」）
		Canvas.SetContent(MoveTemp(Cmds), MoveTemp(Hits), Bg);
	}
}
