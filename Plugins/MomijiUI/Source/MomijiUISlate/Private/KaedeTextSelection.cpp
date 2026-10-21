// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeTextSelection.cpp — 文字の選択とコピー（2026-10-07・設計 Docs/Reports/2026/10/07/01・仕様 momiji-ui-framework.md §7「選択」）。
// 前半は選択の規則（FKaedeTextSelection）、後半は SKaedeUiCanvas からの繋ぎ（集める・押す・伸ばす・離す・描く）。
// 2026-10-18 に SKaedeUiCanvas.cpp から分けた（Issue #73）。
#include "KaedeTextSelection.h"
#include "KaedeUiCanvasDraw.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"   // ClipboardCopy

DEFINE_LOG_CATEGORY_STATIC(LogKaedeTextSelection, Log, All);

namespace KaedeUI
{
	TArray<int32> CpStarts(const FString& S)
	{
		TArray<int32> Out;
		Out.Reserve(S.Len() + 1);
		for (int32 i = 0; i < S.Len();)
		{
			Out.Add(i);
			i += (S[i] >= 0xD800 && S[i] <= 0xDBFF && i + 1 < S.Len() && S[i + 1] >= 0xDC00 && S[i + 1] <= 0xDFFF) ? 2 : 1;
		}
		Out.Add(S.Len());
		return Out;
	}

	void FKaedeTextSelection::ResetCollected()
	{
		Lines.Reset();
		Areas.Reset();
		GroupLines.Reset();
	}

	void FKaedeTextSelection::AddCmd(const FKaedeDrawCmd& C, const FString& ScrollId, int32 FontSize)
	{
		if (C.Kind == EKaedeDrawKind::SelectArea)
		{
			FArea A;
			A.Group = C.SelGroup; A.ScrollId = ScrollId;
			A.X = C.X; A.Y = C.Y; A.W = C.W; A.H = C.H;
			A.FocusGroup = C.FocusGroup;
			Areas.Add(A);
			return;
		}
		if (C.Kind != EKaedeDrawKind::Text || C.SelGroup.IsEmpty()) return;
		FLine L;
		L.Group = C.SelGroup; L.Text = C.Text; L.ScrollId = ScrollId;
		L.Run = C.SelRun;
		L.X = C.X; L.Y = C.Y; L.LineH = C.LineH > 0.f ? C.LineH : (C.FontPt > 0.f ? C.FontPt : (float)FontSize) * 1.4f;
		L.FontPt = C.FontPt; L.bBold = C.bBold;
		GroupLines.FindOrAdd(L.Group).Add(Lines.Add(L));
	}

	bool FKaedeTextSelection::DropIfChanged()
	{
		if (!State.IsSet()) return false;
		const FState& S = State.GetValue();
		for (int32 i = 0; i < S.Texts.Num(); ++i)
		{
			const FLine* Ln = LineOf(S.Group, S.FirstLine + i);
			if (!Ln || Ln->Text != S.Texts[i])
			{
				State.Reset();
				if (Press.IsSet()) Press->bDead = true;   // ドラッグ中なら、離すまで何もしない
				return true;
			}
		}
		return false;
	}

	const FKaedeTextSelection::FLine* FKaedeTextSelection::LineOf(const FString& Group, int32 Line) const
	{
		const TArray<int32>* Ls = GroupLines.Find(Group);
		return (Ls && Ls->IsValidIndex(Line)) ? &Lines[(*Ls)[Line]] : nullptr;
	}

	float FKaedeTextSelection::PrefixW(const FKaedeSelEnv& E, const FLine& Line, int32 Chars) const
	{
		if (Chars <= 0) return 0.f;
		const TArray<int32> Starts = CpStarts(Line.Text);
		const FString Prefix = Line.Text.Left(Starts[FMath::Min(Chars, Starts.Num() - 1)]);
		const float Px = Line.FontPt > 0.f ? Line.FontPt : (float)E.FontSize;
		if (!FSlateApplication::IsInitialized()) return (float)Prefix.Len() * Px;
		// 描くのと同じ書体・大きさで測る（OnPaint の Text と同じ: 論理px × 描いた倍率 を丸めた大きさ）。描いた位置と当たりが同じ関数から出る
		const float Scale = E.Scale > 0.f ? E.Scale : 1.f;
		const FSlateFontInfo Font = MakeKaedeUiFont(Line.bBold, FMath::Max(1, FMath::RoundToInt(Px * Scale)), E.Family);
		return (float)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Prefix, Font).X / Scale;
	}

	int32 FKaedeTextSelection::ChAt(const FKaedeSelEnv& E, const FLine& Line, float Dx) const
	{
		const int32 N = FMath::Max(0, CpStarts(Line.Text).Num() - 1);
		if (Dx <= 0.f || N == 0) return 0;
		if (Dx >= PrefixW(E, Line, N)) return N;
		// Dx を含む文字 k（W(k) <= Dx < W(k+1)）を二分探索し、その字の中点より左なら k、右なら k+1
		int32 Lo = 0, Hi = N - 1;
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi + 1) / 2;
			if (PrefixW(E, Line, Mid) <= Dx) Lo = Mid; else Hi = Mid - 1;
		}
		const float W0 = PrefixW(E, Line, Lo), W1 = PrefixW(E, Line, Lo + 1);
		return Dx < (W0 + W1) * 0.5f ? Lo : Lo + 1;
	}

	FString FKaedeTextSelection::AreaAt(const FKaedeSelEnv& E, FVector2f L, int32 TopGroup) const
	{
		for (int32 i = Areas.Num() - 1; i >= 0; --i)   // 後に出た器＝内側の器が勝つ
		{
			const FArea& A = Areas[i];
			if (A.FocusGroup != TopGroup || !GroupLines.Contains(A.Group)) continue;
			float Y = A.Y;
			if (!A.ScrollId.IsEmpty())
			{
				const FVector4f* Vp = E.Viewport(A.ScrollId);
				if (Vp && (L.Y < Vp->Y || L.Y >= Vp->Y + Vp->W || L.X < Vp->X || L.X >= Vp->X + Vp->Z)) continue;   // 見える範囲の外
				Y -= E.ScrollOffset(A.ScrollId);
			}
			if (L.X >= A.X && L.X < A.X + A.W && L.Y >= Y && L.Y < Y + A.H) return A.Group;
		}
		return FString();
	}

	FKaedeTextSelection::FPos FKaedeTextSelection::PosAt(const FKaedeSelEnv& E, const FString& Group, FVector2f L) const
	{
		FPos Out;
		const TArray<int32>* Ls = GroupLines.Find(Group);
		if (!Ls || Ls->Num() == 0) return Out;
		// 行: 縦の当たり [Y, Y+LineH) に入る行、無ければ縦にいちばん近い行。同じ距離（同じ段に並んだ片など）なら横に近い行、それも同じなら先の行（§4.2）
		auto VDist = [&E, &L](const FLine& Ln)
		{
			const float Ly = L.Y + (Ln.ScrollId.IsEmpty() ? 0.f : E.ScrollOffset(Ln.ScrollId));
			return Ly < Ln.Y ? Ln.Y - Ly : (Ly >= Ln.Y + Ln.LineH ? Ly - (Ln.Y + Ln.LineH) : 0.f);
		};
		float BestV = TNumericLimits<float>::Max();
		for (int32 K : *Ls) BestV = FMath::Min(BestV, VDist(Lines[K]));
		int32 Best = -1;
		float BestH = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Ls->Num(); ++i)
		{
			const FLine& Ln = Lines[(*Ls)[i]];
			if (VDist(Ln) > BestV) continue;
			const float Adv = PrefixW(E, Ln, CpStarts(Ln.Text).Num() - 1);
			const float H = L.X < Ln.X ? Ln.X - L.X : (L.X > Ln.X + Adv ? L.X - (Ln.X + Adv) : 0.f);
			if (H < BestH) { BestH = H; Best = i; }
		}
		Out.Line = FMath::Max(0, Best);
		const FLine& Ln = Lines[(*Ls)[Out.Line]];
		Out.Ch = ChAt(E, Ln, L.X - Ln.X);
		return Out;
	}

	FString FKaedeTextSelection::TextOf(const FString& Group, FPos A, FPos B) const
	{
		if (B.Line < A.Line || (B.Line == A.Line && B.Ch < A.Ch)) Swap(A, B);
		// 行ごとに文字番号の範囲の部分文字列。同じ Label の折り返しの続きは何も挟まず、Label が変わるところは改行。先頭・末尾の改行は付けない（§4.5）
		FString Out;
		int32 PrevRun = INDEX_NONE;
		for (int32 Li = A.Line; Li <= B.Line; ++Li)
		{
			const FLine* Ln = LineOf(Group, Li);
			if (!Ln) break;
			const TArray<int32> Starts = CpStarts(Ln->Text);
			const int32 N = Starts.Num() - 1;
			const int32 C0 = FMath::Clamp(Li == A.Line ? A.Ch : 0, 0, N), C1 = FMath::Clamp(Li == B.Line ? B.Ch : N, 0, N);
			if (Li > A.Line && Ln->Run != PrevRun) Out += TEXT("\n");
			if (C1 > C0) Out += Ln->Text.Mid(Starts[C0], Starts[C1] - Starts[C0]);
			PrevRun = Ln->Run;
		}
		while (Out.StartsWith(TEXT("\n"))) Out.RightChopInline(1);
		while (Out.EndsWith(TEXT("\n"))) Out.LeftChopInline(1);
		return Out;
	}

	bool FKaedeTextSelection::IsOverText(const FKaedeSelEnv& E, const FString& Group, FVector2f L) const
	{
		const TArray<int32>* Ls = GroupLines.Find(Group);
		if (!Ls) return false;
		// 選べる行の上（行の縦の当たりの中・字の幅の中）だけ
		for (int32 K : *Ls)
		{
			const FLine& Ln = Lines[K];
			const float Ly = L.Y + (Ln.ScrollId.IsEmpty() ? 0.f : E.ScrollOffset(Ln.ScrollId));
			if (Ly < Ln.Y || Ly >= Ln.Y + Ln.LineH || L.X < Ln.X) continue;
			if (L.X <= Ln.X + PrefixW(E, Ln, CpStarts(Ln.Text).Num() - 1)) return true;
		}
		return false;
	}

	void FKaedeTextSelection::Begin(const FKaedeSelEnv& E)
	{
		// 成立: 押した点を anchor、いまの点を focus にする（どちらも押した器の中で解く）
		FPress& P = Press.GetValue();
		P.bDragging = true;
		FState S;
		S.Group = P.Group;
		S.Anchor = PosAt(E, P.Group, P.P);
		S.Focus = S.Anchor;
		State = S;
	}

	void FKaedeTextSelection::ExtendTo(const FKaedeSelEnv& E, FVector2f L)
	{
		if (!State.IsSet()) return;
		FState& S = State.GetValue();
		S.Focus = PosAt(E, S.Group, L);
		// 範囲の行の文字を控える（描き直しで変わったら消すため。§4.4）
		S.FirstLine = FMath::Min(S.Anchor.Line, S.Focus.Line);
		const int32 Last = FMath::Max(S.Anchor.Line, S.Focus.Line);
		S.Texts.Reset();
		for (int32 Li = S.FirstLine; Li <= Last; ++Li)
		{
			const FLine* Ln = LineOf(S.Group, Li);
			S.Texts.Add(Ln ? Ln->Text : FString());
		}
	}

	bool FKaedeTextSelection::Ordered(FPos& OutA, FPos& OutB) const
	{
		if (!State.IsSet()) return false;
		const FState& S = State.GetValue();
		const bool bFwd = S.Anchor.Line < S.Focus.Line || (S.Anchor.Line == S.Focus.Line && S.Anchor.Ch <= S.Focus.Ch);
		OutA = bFwd ? S.Anchor : S.Focus;
		OutB = bFwd ? S.Focus : S.Anchor;
		return true;
	}
}

// ===== SKaedeUiCanvas からの繋ぎ =====

KaedeUI::FKaedeSelEnv SKaedeUiCanvas::SelEnv() const
{
	KaedeUI::FKaedeSelEnv E;
	E.FontSize = FontSize;
	E.Family = FontFamily;
	E.Scale = PaintedScale();
	E.ScrollOffset = [this](const FString& Id) { return ScrollOffsetFor(Id); };
	E.Viewport = [this](const FString& Id) { return ScrollViewports.Find(Id); };
	return E;
}

void SKaedeUiCanvas::CollectSelectables()
{
	Sel->ResetCollected();
	WalkCmds(/*bWithGhosts=*/false, [this](int32, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)
	{
		Sel->AddCmd(C, Clip ? Clip->Sprite : FString(), FontSize);
		return true;
	});
}

void SKaedeUiCanvas::VerifySelection()
{
	Sel->DropIfChanged();
}

FString SKaedeUiCanvas::SelectableGroupAt(FVector2f L) const
{
	// 押せる当たり（action のある部品）の上は従来の押下。Modal の暗幕（action 無し）は器の群と最前面の群で弾く
	const int32 Hit = HitAtLogical(L);
	if (Hits.IsValidIndex(Hit) && !Hits[Hit].Action.IsEmpty()) return FString();
	return Sel->AreaAt(SelEnv(), L, TopFocusGroup());
}

void SKaedeUiCanvas::SelExtendTo(FVector2f L)
{
	if (!Sel->State.IsSet()) return;
	Sel->ExtendTo(SelEnv(), L);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::ClearSelection()
{
	if (!Sel->State.IsSet()) return;
	Sel->State.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::SelFinish(FVector2f L)
{
	SelExtendTo(L);
	FKaedeSelectionInfo Info;
	if (!GetSelection(Info)) return;
	if (Info.FromLine == Info.ToLine && Info.FromCh == Info.ToCh) { ClearSelection(); return; }   // 範囲が空なら何もしない
	if (Info.Text.IsEmpty()) return;
	FPlatformApplicationMisc::ClipboardCopy(*Info.Text);
	UE_LOG(LogKaedeTextSelection, Display, TEXT("[ui] select copied %s"), *DescribeSelection());
	// 「コピーしました」を 900ms。描き直しはこの間だけ毎フレーム（キャンバスは常時描き直す作りではない）
	Sel->CopiedAt = FPlatformTime::Seconds();
	Sel->CopiedPos = L;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return (Sel->CopiedAt >= 0.0 && FPlatformTime::Seconds() - Sel->CopiedAt < 0.95) ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
	}));
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SKaedeUiCanvas::SelClickThrough(FVector2f L)
{
	// 4px 動かずに離した＝従来どおりのクリック（押した点に押せる当たりは無い）: 選択を消し、編集中の欄を降り、背景のクリックを渡す
	ClearSelection();
	LeaveTextEditor();
	if (HitAtLogical(L) < 0 && OnBackgroundClick) { OnBackgroundClick(FVector2D(L.X, L.Y)); }
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool SKaedeUiCanvas::GetSelection(FKaedeSelectionInfo& Out) const
{
	KaedeUI::FKaedeTextSelection::FPos A, B;
	if (!Sel->Ordered(A, B)) return false;
	Out.Group = Sel->State->Group;
	Out.FromLine = A.Line; Out.FromCh = A.Ch;
	Out.ToLine = B.Line; Out.ToCh = B.Ch;
	Out.Text = Sel->TextOf(Out.Group, A, B);
	return true;
}

FString SKaedeUiCanvas::DescribeSelection() const
{
	FKaedeSelectionInfo I;
	if (!GetSelection(I)) return TEXT("none");
	return FString::Printf(TEXT("group=%s from=%d,%d to=%d,%d text=%s"), *I.Group, I.FromLine, I.FromCh, I.ToLine, I.ToCh,
		*I.Text.Replace(TEXT("\n"), TEXT("\\n")));
}

void SKaedeUiCanvas::SelectTextProbe(FVector2f P0, FVector2f P1)
{
	const FGeometry Geo = FGeometry::MakeRoot(FVector2f((float)LogicalW, (float)LogicalH), FSlateLayoutTransform());
	const FPointerEvent Down(0, P0, P0, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	OnMouseButtonDown(Geo, Down);
	const TSet<FKey> Held = { EKeys::LeftMouseButton };
	const FPointerEvent Move(0, P1, P0, Held, EKeys::Invalid, 0.f, FModifierKeysState());
	OnMouseMove(Geo, Move);
	const FPointerEvent Up(0, P1, P1, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
	OnMouseButtonUp(Geo, Up);
}

FCursorReply SKaedeUiCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (Sel->Press.IsSet() && Sel->Press->bDragging) return FCursorReply::Cursor(EMouseCursor::TextEditBeam);
	const FVector2f L = ToLogical(MyGeometry, CursorEvent);
	const FString Group = SelectableGroupAt(L);
	if (Group.IsEmpty()) return FCursorReply::Unhandled();
	return Sel->IsOverText(SelEnv(), Group, L) ? FCursorReply::Cursor(EMouseCursor::TextEditBeam) : FCursorReply::Unhandled();
}

// 文字の選択の反転と「コピーしました」の札（設計 10/07/01 §4.6）。
// 反転は**文字の上に半透明で重ねる**（文字の段 Layer+1 より上の Layer+2）。下に敷くと別ブラシの板に沈む（adv-ue-workspace.md §7）。
// 配置には影響しない（当たり・焦点・高さは変わらない）。ScrollView の中の行はその見える範囲で切る
void SKaedeUiCanvas::PaintSelection(const KaedeUI::FKaedePaintCtx& P, int32 Layer) const
{
	const float Scale = P.Scale;
	KaedeUI::FKaedeTextSelection::FPos A, B;
	if (Sel->Ordered(A, B))
	{
		const KaedeUI::FKaedeSelEnv E = SelEnv();
		const FString& Group = Sel->State->Group;
		const FLinearColor SelTint = FLinearColor::FromSRGBColor(FColor(0xe0, 0xa0, 0x4a, 89));   // T.accent の 35%
		for (int32 Li = A.Line; Li <= B.Line; ++Li)
		{
			const KaedeUI::FKaedeTextSelection::FLine* Ln = Sel->LineOf(Group, Li);
			if (!Ln) continue;
			float Y = Ln->Y;
			const FVector4f* Vp = Ln->ScrollId.IsEmpty() ? nullptr : ScrollViewports.Find(Ln->ScrollId);
			if (Vp)
			{
				Y -= ScrollOffsetFor(Ln->ScrollId);
				if (Y + Ln->LineH <= Vp->Y || Y >= Vp->Y + Vp->W) continue;   // 見える範囲の外は測らない
			}
			else if (Y + Ln->LineH <= 0.f || Y >= (float)LogicalH) continue;
			const int32 N = FMath::Max(0, KaedeUI::CpStarts(Ln->Text).Num() - 1);
			const int32 C0 = Li == A.Line ? A.Ch : 0, C1 = Li == B.Line ? B.Ch : N;
			if (C1 <= C0) continue;
			const float X0 = Ln->X + Sel->PrefixW(E, *Ln, C0), X1 = Ln->X + Sel->PrefixW(E, *Ln, C1);
			if (Vp) P.PushClip(Vp->X, Vp->Y, Vp->Z, Vp->W);
			FSlateDrawElement::MakeBox(P.Out, Layer + 2, P.Box(X0, Y, (X1 - X0) * Scale, Ln->LineH * Scale), P.White, ESlateDrawEffect::None, SelTint);
			if (Vp) P.Out.PopClip();
		}
	}
	if (Sel->CopiedAt >= 0.0 && FPlatformTime::Seconds() - Sel->CopiedAt < 0.9)
	{
		// 離した点の右下に小さな札（キャンバスの飾り。描画命令ではない＝照合の prims に入らない）
		const FString Msg(TEXT("コピーしました"));
		constexpr float MsgPx = 13.f, Pad = 6.f;
		const FSlateFontInfo Font = KaedeUI::MakeKaedeUiFont(false, FMath::Max(1, FMath::RoundToInt(MsgPx * Scale)), FontFamily);
		FVector2f TextSize(MsgPx * 7.f, MsgPx * 1.4f);
		if (FSlateApplication::IsInitialized())
		{
			TextSize = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Msg, Font)) / Scale;
		}
		const float W = TextSize.X + 2.f * Pad, H = TextSize.Y + 2.f * Pad;
		const float X = FMath::Clamp(Sel->CopiedPos.X + 12.f, 0.f, FMath::Max(0.f, (float)LogicalW - W));
		const float Y = FMath::Clamp(Sel->CopiedPos.Y + 16.f, 0.f, FMath::Max(0.f, (float)LogicalH - H));
		FSlateDrawElement::MakeBox(P.Out, Layer + 2, P.Box(X, Y, W * Scale, H * Scale), KaedeUI::GetKaedeRoundedFillBrush(4.f * Scale), ESlateDrawEffect::None,
			FLinearColor::FromSRGBColor(FColor(0x15, 0x1a, 0x1e, 0xe6)));
		FSlateDrawElement::MakeText(P.Out, Layer + 3, P.Box(X + Pad, Y + Pad, TextSize.X * Scale, TextSize.Y * Scale), Msg, Font, ESlateDrawEffect::None, FLinearColor::White);
	}
}
