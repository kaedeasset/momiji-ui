// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiCanvasDraw.cpp — SKaedeUiCanvas::OnPaint の部品（宣言と説明は KaedeUiCanvasDraw.h）。
// 2026-10-18 に OnPaint から分けた（Issue #73）。描き方そのものは分ける前と同じ。
#include "KaedeUiCanvasDraw.h"

#include "ui/Tween.hpp"   // UiMotionSample
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace KaedeUI
{
	FKaedePaintCtx::FKaedePaintCtx(const FGeometry& InGeo, FSlateWindowElementList& InOut, float InScale, float InOffX, float InOffY,
	                               int32 InFontSize, EKaedeFontFamily InFamily)
		: Geo(InGeo), Out(InOut), Scale(InScale), OffX(InOffX), OffY(InOffY)
		, White(FCoreStyle::Get().GetBrush("WhiteBrush")), FontSize(InFontSize), Family(InFamily)
	{
	}

	// ===== 重ね順の段 =====

	void FKaedeLayerStack::NoteHi(float X, float Y, float W, float H)
	{
		if (!bHiSince) { HiMinX = X; HiMinY = Y; HiMaxX = X + W; HiMaxY = Y + H; bHiSince = true; return; }
		HiMinX = FMath::Min(HiMinX, X); HiMinY = FMath::Min(HiMinY, Y);
		HiMaxX = FMath::Max(HiMaxX, X + W); HiMaxY = FMath::Max(HiMaxY, Y + H);
	}

	void FKaedeLayerStack::Raise()
	{
		Layer += 2;
		bHiSince = false;
		RectsSince.Reset();
	}

	void FKaedeLayerStack::Before(const FKaedeDrawCmd& C, float Scale, int32 FontSize)
	{
		switch (C.Kind)
		{
		case EKaedeDrawKind::Rect:
		case EKaedeDrawKind::RectOutline:
		case EKaedeDrawKind::Polygon:
		case EKaedeDrawKind::Gradient:
		{
			if (bHiSince && C.X < HiMaxX && C.X + C.W > HiMinX && C.Y < HiMaxY && C.Y + C.H > HiMinY) Raise();
			// ブラシは半径で決まる（GetKaedeRoundedFillBrush と同じ量子化・直角は 0）。濃淡（-1）・多角形（-2）・枠（-3）も別のバッチ（2026-10-12）
			const int32 BrushKey = C.Kind == EKaedeDrawKind::Gradient ? -1 : C.Kind == EKaedeDrawKind::Polygon ? -2 : C.Kind == EKaedeDrawKind::RectOutline ? -3
				: (C.Radius > 0.f ? FMath::Clamp(FMath::RoundToInt(C.Radius * Scale), 1, 128) : 0);
			for (const FRectSince& R : RectsSince)
			{
				if (R.BrushKey != BrushKey && C.X < R.MaxX && C.X + C.W > R.MinX && C.Y < R.MaxY && C.Y + C.H > R.MinY)
				{
					Raise();
					break;
				}
			}
			FRectSince* Same = RectsSince.FindByPredicate([BrushKey](const FRectSince& R) { return R.BrushKey == BrushKey; });
			if (!Same) { Same = &RectsSince.AddDefaulted_GetRef(); Same->BrushKey = BrushKey; Same->MinX = C.X; Same->MinY = C.Y; Same->MaxX = C.X + C.W; Same->MaxY = C.Y + C.H; }
			else
			{
				Same->MinX = FMath::Min(Same->MinX, C.X); Same->MinY = FMath::Min(Same->MinY, C.Y);
				Same->MaxX = FMath::Max(Same->MaxX, C.X + C.W); Same->MaxY = FMath::Max(Same->MaxY, C.Y + C.H);
			}
			break;
		}
		case EKaedeDrawKind::Text:
		{
			// Text コマンドは幅を持たない。折り返し幅か、文字数×字の大きさで見積もる（過大でも段が1つ増えるだけ）。
			const float Px = C.FontPt > 0.f ? C.FontPt : (float)FontSize;
			NoteHi(C.X, C.Y, C.WrapW > 0 ? (float)C.WrapW : (float)C.Text.Len() * Px, C.CenterH > 0.f ? C.CenterH : Px * 1.4f);
			break;
		}
		case EKaedeDrawKind::Lines:
			for (const FVector2f& P : C.Points) { NoteHi(P.X, P.Y, 0.f, 0.f); }
			break;
		case EKaedeDrawKind::Image:
		case EKaedeDrawKind::Ellipse:
		case EKaedeDrawKind::TextField:
			NoteHi(C.X, C.Y, C.W, C.H);
			break;
		default:
			break;
		}
	}

	// ===== 合成 =====

	void ShiftKaedeCmdY(FKaedeDrawCmd& C, float D)
	{
		// 忘れると「他は動くのに線だけ取り残される」形で静かにずれる（画面遷移で毎回出る）。
		C.Y += D;
		for (FVector2f& P : C.Points) P.Y += D;
	}

	void ScaleKaedeCmdAbout(FKaedeDrawCmd& C, float Px, float Py, float S)
	{
		C.X = Px + (C.X - Px) * S;
		C.Y = Py + (C.Y - Py) * S;
		for (FVector2f& P : C.Points)   // Lines（F-2）も同じ中心で縮める
		{
			P.X = Px + (P.X - Px) * S;
			P.Y = Py + (P.Y - Py) * S;
		}
		if (C.Kind == EKaedeDrawKind::Rect || C.Kind == EKaedeDrawKind::RectOutline
			|| C.Kind == EKaedeDrawKind::Image || C.Kind == EKaedeDrawKind::Ellipse)
		{
			C.W *= S;
			C.H *= S;
		}
	}

	void BlendKaedeCmdColor(FKaedeDrawCmd& C, float FbMul, float FbWhite)
	{
		if (FbMul == 1.f && FbWhite <= 0.f) return;
		auto Blend = [&](uint8 V)
		{
			float F = (float)V / 255.f * FbMul;
			F += (1.f - F) * FbWhite;
			return (uint8)FMath::Clamp(FMath::RoundToInt(F * 255.f), 0, 255);
		};
		C.Color = FColor(Blend(C.Color.R), Blend(C.Color.G), Blend(C.Color.B), C.Color.A);
	}

	void ApplyKaedeMotion(FKaedeDrawCmd& C, const kaedevn::ui::UiMotionSample& MS, float ScrollOffset)
	{
		C.X += MS.offX;
		C.Y += MS.offY;
		for (FVector2f& P : C.Points) { P.X += MS.offX; P.Y += MS.offY; }
		// textFixed（pulse 単独）のときは文字に scale を掛けない＝箱だけが脈動し、文字は大きさも位置も不変（仕様 §8）
		if (MS.scale != 1.f && !(MS.textFixed && C.Kind == EKaedeDrawKind::Text))
		{
			// ピボット（anim ノード矩形の中心）は offset とスクロールシフトを同様に
			// 適用した位置基準で追従。テキストはフォントサイズを変えず位置のみ寄せる。
			ScaleKaedeCmdAbout(C, C.PivotX + MS.offX, C.PivotY + MS.offY - ScrollOffset, MS.scale);
		}
		if (MS.alpha < 1.f)
		{
			C.Color.A = (uint8)FMath::RoundToInt((float)C.Color.A * MS.alpha);
			C.Alpha *= MS.alpha;   // Image の不透明度
		}
		if (MS.hasText && C.Kind == EKaedeDrawKind::Text)
			C.Text = UTF8_TO_TCHAR(MS.text.c_str());
		// 2026-10-12: 縦に開く・閉じる（中心から）
		if (MS.scaleY != 1.f)
		{
			const float Py = C.PivotY + MS.offY - ScrollOffset;
			C.Y = Py + (C.Y - Py) * MS.scaleY;
			for (FVector2f& P : C.Points) P.Y = Py + (P.Y - Py) * MS.scaleY;
			if (C.Kind == EKaedeDrawKind::Text) { if (MS.scaleY < 0.6f) C.Color.A = 0; }
			else C.H *= MS.scaleY;
		}
		if (MS.frame >= 0 && C.Kind == EKaedeDrawKind::Image && C.Src[2] > 0.f) C.Src[0] += C.Src[2] * MS.frame;
		if (MS.valueScale != 1.f && C.bFillBar) C.W *= FMath::Clamp(MS.valueScale, 0.f, 2.f);
	}

	bool ApplyKaedeFocusStyle(const SKaedeUiCanvas::FFocusStyle& S, FKaedeDrawCmd& C, bool bFillDone,
	                          const FKaedeHit* FocusHit, float HitY, const FKaedePaintCtx& P, int32 Layer)
	{
		if (S.bText && C.Kind == EKaedeDrawKind::Text) C.Color = FColor(S.Text.R, S.Text.G, S.Text.B, C.Color.A);
		if (!S.bFill || bFillDone) return bFillDone;
		const bool bG = S.GradColors.Num() >= 2;
		auto GradAtT = [&](float t) -> FColor
		{
			// 止め色の間を線形に
			for (int32 g = 1; g < S.GradColors.Num(); ++g)
				if (t <= S.GradPos[g] || g == S.GradColors.Num() - 1)
				{
					const float a = S.GradPos[g - 1], b = S.GradPos[g];
					const float u = b > a ? FMath::Clamp((t - a) / (b - a), 0.f, 1.f) : 0.f;
					const FColor& A = S.GradColors[g - 1]; const FColor& B = S.GradColors[g];
					return FColor((uint8)FMath::Lerp((float)A.R, (float)B.R, u), (uint8)FMath::Lerp((float)A.G, (float)B.G, u),
					              (uint8)FMath::Lerp((float)A.B, (float)B.B, u), (uint8)FMath::Lerp((float)A.A, (float)B.A, u));
				}
			return S.GradColors[0];
		};
		if (C.Kind == EKaedeDrawKind::Rect)
		{
			if (bG) { C.Kind = EKaedeDrawKind::Gradient; C.GradColors = S.GradColors; C.GradPos = S.GradPos; C.bGradH = S.bGradH; C.Color = FColor::White; }
			else C.Color = S.Fill;
			return true;
		}
		if (C.Kind == EKaedeDrawKind::Gradient)
		{
			if (bG) { C.GradColors = S.GradColors; C.GradPos = S.GradPos; C.bGradH = S.bGradH; }
			else { C.GradColors = {S.Fill, S.Fill}; C.GradPos = {0.f, 1.f}; }
			return true;
		}
		if (C.Kind == EKaedeDrawKind::Polygon)
		{
			C.PointColors.Reset();
			if (bG)
				for (const FVector2f& Pt : C.Points)
					C.PointColors.Add(GradAtT(S.bGradH ? (C.W > 0.f ? (Pt.X - C.X) / C.W : 0.f) : (C.H > 0.f ? (Pt.Y - C.Y) / C.H : 0.f)));
			else C.Color = S.Fill;
			return true;
		}
		if (!FocusHit) return false;
		// 地の無い項目（文字だけ）: 当たりの矩形に地と枠を敷いてから、この命令を描く
		const FKaedeHit& FH = *FocusHit;
		const float Scale = P.Scale;
		const FPaintGeometry FPG = P.Box(FH.X, HitY, FH.W * Scale, FH.H * Scale);
		if (bG)
		{
			TArray<FSlateGradientStop> Stops;
			for (int32 g = 0; g < S.GradColors.Num(); ++g)
				Stops.Add(FSlateGradientStop(FVector2f(S.GradPos[g] * FH.W * Scale, S.GradPos[g] * FH.H * Scale), FLinearColor::FromSRGBColor(S.GradColors[g])));
			FSlateDrawElement::MakeGradient(P.Out, Layer, FPG, Stops, S.bGradH ? Orient_Vertical : Orient_Horizontal);
		}
		else if (S.Fill.A > 0)
			FSlateDrawElement::MakeBox(P.Out, Layer, FPG, P.White, ESlateDrawEffect::None, FLinearColor::FromSRGBColor(S.Fill));
		if (S.bBorder && S.Border.A > 0)
		{
			const FLinearColor BC = FLinearColor::FromSRGBColor(S.Border);
			const float Th = FMath::Max(1.f, 2.f * Scale);
			auto Bar = [&](float lx, float ly, float w, float h)
			{
				FSlateDrawElement::MakeBox(P.Out, Layer, P.Box(lx, ly, w, h), P.White, ESlateDrawEffect::None, BC);
			};
			Bar(FH.X, HitY, FH.W * Scale, Th);
			Bar(FH.X, HitY + FH.H - Th / Scale, FH.W * Scale, Th);
			Bar(FH.X, HitY, Th, FH.H * Scale);
			Bar(FH.X + FH.W - Th / Scale, HitY, Th, FH.H * Scale);
		}
		return true;
	}

	// ===== Kind ごとの描き方 =====

	namespace
	{
		// 2026-10-12: 回す・斜めにする（Angle・SkewX。軸は XfX,XfY）。点を回す・描く形を回す（PaintGeometry にレンダー変形を足す）
		struct FCmdXf
		{
			FCmdXf(const FKaedePaintCtx& InP, const FKaedeDrawCmd& InC) : P(InP), C(InC), bOn(InC.Angle != 0.f || InC.SkewX != 0.f)
			{
				if (!bOn) return;
				const float a = FMath::DegreesToRadians(C.Angle), k = FMath::Tan(FMath::DegreesToRadians(C.SkewX));
				const float c = FMath::Cos(a), s = FMath::Sin(a);
				// 行ベクトル: まず斜め（x' = x + k y）、次に回す
				M = FMatrix2x2f(c, s, k * c - s, k * s + c);
			}
			const FKaedePaintCtx& P;
			const FKaedeDrawCmd& C;
			bool bOn;
			FMatrix2x2f M;

			// LX,LY は論理 px の左上、LW,LH は描く大きさ（px・Scale 済み）
			FPaintGeometry PG(float LX, float LY, float LW, float LH) const
			{
				const FVector2f Pos = P.ToLocal(LX, LY);
				if (!bOn || LW <= 0.f || LH <= 0.f)
					return P.Geo.ToPaintGeometry(FVector2f(LW, LH), FSlateLayoutTransform(Pos));
				const FVector2f Piv = P.ToLocal(C.XfX, C.XfY);
				return P.Geo.ToPaintGeometry(FVector2f(LW, LH), FSlateLayoutTransform(Pos),
					FSlateRenderTransform(M), FVector2f((Piv.X - Pos.X) / LW, (Piv.Y - Pos.Y) / LH));
			}
			FVector2f Point(FVector2f Pt) const
			{
				if (!bOn) return Pt;
				const FVector2f D = M.TransformPoint(FVector2f(Pt.X - C.XfX, Pt.Y - C.XfY));
				return FVector2f(C.XfX + D.X, C.XfY + D.Y);
			}
		};
	}

	void DrawKaedeRect(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		const FCmdXf Xf(P, C);
		const FSlateBrush* Brush = C.Radius > 0.f ? GetKaedeRoundedFillBrush(C.Radius * P.Scale) : P.White;
		FSlateDrawElement::MakeBox(P.Out, Layer, Xf.PG(C.X, C.Y, C.W * P.Scale, C.H * P.Scale), Brush, ESlateDrawEffect::None,
			FLinearColor::FromSRGBColor(C.Color));
	}

	void DrawKaedeRectOutline(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		const FCmdXf Xf(P, C);
		const float Scale = P.Scale;
		const FLinearColor Tint = FLinearColor::FromSRGBColor(C.Color);
		if (C.Radius > 0.f)
		{
			// RoundedBox の塗りは「要素Tint」で決まる（ブラシ側の Transparent 指定は効かない）。
			// 枠線だけ残すため Tint はアルファ0を渡す（枠色はブラシに焼き込み済み）。
			FSlateDrawElement::MakeBox(P.Out, Layer, Xf.PG(C.X, C.Y, C.W * Scale, C.H * Scale),
				GetKaedeRoundedOutlineBrush(C.Radius * Scale, Tint, FMath::Max(1.f, C.Thickness * Scale)),
				ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.f));
			return;
		}
		// 太さ（2026-10-12: 太い枠は 1 本の太い線・内側へ）
		const float Th = FMath::Max(1.f, C.Thickness * Scale);
		const float ThL = Th / Scale;
		auto Bar = [&](float lx, float ly, float w, float h)
		{
			FSlateDrawElement::MakeBox(P.Out, Layer, Xf.PG(lx, ly, w, h), P.White, ESlateDrawEffect::None, Tint);
		};
		if (C.Thickness <= 1.f)
		{
			Bar(C.X, C.Y, C.W * Scale, Th);
			Bar(C.X, C.Y + C.H, C.W * Scale, Th);
			Bar(C.X, C.Y, Th, C.H * Scale);
			Bar(C.X + C.W, C.Y, Th, C.H * Scale);
		}
		else
		{
			Bar(C.X, C.Y, C.W * Scale, Th);
			Bar(C.X, C.Y + C.H - ThL, C.W * Scale, Th);
			Bar(C.X, C.Y, Th, C.H * Scale);
			Bar(C.X + C.W - ThL, C.Y, Th, C.H * Scale);
		}
	}

	// ---- 図形（F-2）----
	// Slate は折れ線を素で持っている（MakeLines）。足りなかったのはここへの繋ぎだけで、
	// 曲線は生成器側が折れ線に割ってから渡す約束にしてある（スプラインの型を持たない）。
	void DrawKaedeLines(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		if (C.Points.Num() < 2) return;
		const FCmdXf Xf(P, C);
		TArray<FVector2D> Pts;
		Pts.Reserve(C.Points.Num());
		for (const FVector2f& P0 : C.Points)
		{
			const FVector2f Pt = Xf.Point(P0);
			const FVector2f L = P.ToLocal(Pt.X, Pt.Y);
			Pts.Add(FVector2D(L.X, L.Y));
		}
		FSlateDrawElement::MakeLines(P.Out, Layer + 1, P.Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None,
			FLinearColor::FromSRGBColor(C.Color), /*bAntialias=*/true, FMath::Max(1.f, C.Thickness * P.Scale));
	}

	void DrawKaedeEllipse(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		const float Scale = P.Scale;
		const FLinearColor Tint = FLinearColor::FromSRGBColor(C.Color);
		// Slate に楕円は無い。**円（w==h）**は角丸ボックスの半径を半分にすると滑らかに描ける（既存の角丸ブラシ）。
		// **縦横が違う楕円**は角丸ボックスだとカプセル形になる（2026-10-03 にブラウザの絵と並べて発覚）ので、
		// 塗りは横の帯を積み、輪郭は 64 辺の折れ線で描く。
		if (FMath::Abs(C.W - C.H) > 0.5f)
		{
			const float Cx = C.X + C.W * 0.5f, Cy = C.Y + C.H * 0.5f, Rx = C.W * 0.5f, Ry = C.H * 0.5f;
			if (C.bFill)
			{
				const int32 Rows = FMath::Max(1, FMath::CeilToInt(C.H * Scale));
				const float RowH = C.H / (float)Rows;
				for (int32 i = 0; i < Rows; ++i)
				{
					const float Yc = C.Y + (i + 0.5f) * RowH;               // 帯の中心の高さで幅を決める
					const float T = FMath::Clamp((Yc - Cy) / Ry, -1.f, 1.f);
					const float Hw = Rx * FMath::Sqrt(FMath::Max(0.f, 1.f - T * T));
					if (Hw <= 0.f) continue;
					FSlateDrawElement::MakeBox(P.Out, Layer + 1, P.Box(Cx - Hw, C.Y + i * RowH, 2.f * Hw * Scale, RowH * Scale + 0.5f),
						P.White, ESlateDrawEffect::None, Tint);
				}
			}
			else
			{
				TArray<FVector2D> Pts;   // MakeLines は FVector2D（Lines と同じ）
				constexpr int32 N = 64;
				for (int32 i = 0; i <= N; ++i)
				{
					const float A = 2.f * PI * (float)i / (float)N;
					const FVector2f L = P.ToLocal(Cx + Rx * FMath::Cos(A), Cy + Ry * FMath::Sin(A));
					Pts.Add(FVector2D(L.X, L.Y));
				}
				FSlateDrawElement::MakeLines(P.Out, Layer + 1, P.Geo.ToPaintGeometry(), Pts,
					ESlateDrawEffect::None, Tint, true, FMath::Max(1.f, C.Thickness * Scale));
			}
			return;
		}
		const float R = FMath::Min(C.W, C.H) * 0.5f * Scale;
		const FPaintGeometry PG = P.Box(C.X, C.Y, C.W * Scale, C.H * Scale);
		if (C.bFill)
		{
			FSlateDrawElement::MakeBox(P.Out, Layer + 1, PG, GetKaedeRoundedFillBrush(R), ESlateDrawEffect::None, Tint);
		}
		else
		{
			// 輪郭のみ。塗りは要素 Tint で決まるのでアルファ0を渡し、線色はブラシへ焼き込む
			// （RectOutline の角丸経路と同じ理由）。
			FSlateDrawElement::MakeBox(P.Out, Layer + 1, PG,
				GetKaedeRoundedOutlineBrush(R, Tint, FMath::Max(1.f, C.Thickness * Scale)),
				ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
	}

	void DrawKaedeText(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		const float Scale = P.Scale;
		const FCmdXf Xf(P, C);
		const float FontPt = C.FontPt > 0.f ? C.FontPt : (float)P.FontSize;   // ルビ等は小サイズ
		FSlateFontInfo Font = MakeKaedeUiFont(C.bBold, FMath::RoundToInt(FontPt * Scale), P.Family);
		// 2026-10-12: 縁取り（px）・字間（Slate は 1/1000 em）
		if (C.OutlinePx > 0.f)
		{
			Font.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(C.OutlinePx * Scale));
			Font.OutlineSettings.OutlineColor = FLinearColor::FromSRGBColor(C.OutlineColor);
		}
		if (C.LetterSpacing != 0.f)
			Font.LetterSpacing = FMath::RoundToInt(C.LetterSpacing / (FontPt * 4.f / 3.f) * 1000.f);
		FVector2f Pos = P.ToLocal(C.X, C.Y);
		if (C.CenterH > 0.f && FSlateApplication::IsInitialized())
		{
			// 字面で箱の中心へ（2026-10-02）。縦は字面の上下の中心、横（bCenterX＝align center）は
			// 送り幅ではなく字面の左右の中心。フォント固有の補正定数は持たない。
			Pos = KaedeInkCenteredTextPos(C.Text, Font, Pos, C.CenterH * Scale, C.bCenterX);
		}
		const float TW = FMath::Max(1.f, C.WrapW > 0 ? C.WrapW * Scale : 4000.f);
		FPaintGeometry PG = P.Geo.ToPaintGeometry(FVector2f(TW, FontPt * Scale), FSlateLayoutTransform(Pos));
		if (Xf.bOn)
		{
			const FVector2f Piv = P.ToLocal(C.XfX, C.XfY);
			PG = P.Geo.ToPaintGeometry(FVector2f(TW, FontPt * Scale), FSlateLayoutTransform(Pos),
				FSlateRenderTransform(Xf.M), FVector2f((Piv.X - Pos.X) / TW, (Piv.Y - Pos.Y) / (FontPt * Scale)));
		}
		FSlateDrawElement::MakeText(P.Out, Layer + 1, PG, C.Text, Font, ESlateDrawEffect::None, FLinearColor::FromSRGBColor(C.Color));
	}

	void DrawKaedeImage(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C, const FSlateBrush* Brush, float ImgL)
	{
		const float Scale = P.Scale;
		if (!Brush)
		{
			// 読み込み失敗: 沈み色のプレースホルダ矩形（枠は painter が既に描いている）。
			FSlateDrawElement::MakeBox(P.Out, Layer, P.Box(C.X, C.Y, C.W * Scale, C.H * Scale), P.White, ESlateDrawEffect::None,
				FLinearColor(0.f, 0.f, 0.f, 0.25f));
			return;
		}
		const FCmdXf Xf(P, C);
		// 2026-10-12: 9 分割・敷き詰め・切り出し（ブラシを写して設定を変える。Slate は要素を積むときに中身を写す）
		FSlateBrush Local;
		const bool bSlice = C.Slice[0] + C.Slice[1] + C.Slice[2] + C.Slice[3] > 0.f;
		const bool bSrc = C.Src[2] > 0.f && C.Src[3] > 0.f;
		if (bSlice || C.bTile || bSrc)
		{
			Local = *Brush;
			const FVector2D Tex = Brush->ImageSize;
			if (bSlice && Tex.X > 0 && Tex.Y > 0)
			{
				Local.DrawAs = ESlateBrushDrawType::Box;
				Local.Margin = FMargin(C.Slice[0] / Tex.X, C.Slice[1] / Tex.Y, C.Slice[2] / Tex.X, C.Slice[3] / Tex.Y);
				// 角の大きさを画面の倍率に合わせる（ImageSize は描くときの角の基準）
				Local.ImageSize = Tex * Scale;
			}
			if (C.bTile)
			{
				Local.DrawAs = ESlateBrushDrawType::Image;
				Local.Tiling = ESlateBrushTileType::Both;
				Local.ImageSize = Tex * Scale;
			}
			if (bSrc && Tex.X > 0 && Tex.Y > 0)
			{
				Local.SetUVRegion(FBox2f(FVector2f(C.Src[0] / Tex.X, C.Src[1] / Tex.Y), FVector2f((C.Src[0] + C.Src[2]) / Tex.X, (C.Src[1] + C.Src[3]) / Tex.Y)));
				Local.ImageSize = FVector2D(C.Src[2], C.Src[3]);
			}
			Brush = &Local;
		}
		// Fit: contain=枠内にアスペクト維持で収める（レターボックス）/ cover=枠を埋めてクリップ /
		// stretch=枠いっぱいに引き伸ばす。既定 contain。
		const FVector2D Sz = Brush->ImageSize;
		const float ImgAspect = (Sz.Y > 0.0) ? (float)(Sz.X / Sz.Y) : 1.f;
		const float BoxAspect = (C.H > 0.f) ? (C.W / C.H) : 1.f;
		float dw = C.W, dh = C.H, dx = C.X, dy = C.Y;
		if (C.Fit == TEXT("cover"))
		{
			// 枠を埋める（短辺基準）。はみ出しは scissor でクリップ。
			if (ImgAspect > BoxAspect) { dw = C.H * ImgAspect; dx = C.X - (dw - C.W) * 0.5f; }
			else                        { dh = C.W / ImgAspect; dy = C.Y - (dh - C.H) * 0.5f; }
		}
		else if (C.Fit != TEXT("stretch"))
		{
			// contain（既定）: 枠内に収める（長辺基準）。
			if (ImgAspect > BoxAspect) { dh = C.W / ImgAspect; dy = C.Y + (C.H - dh) * 0.5f; }
			else                        { dw = C.H * ImgAspect; dx = C.X + (C.W - dw) * 0.5f; }
		}
		const bool bClip = (C.Fit == TEXT("cover"));
		if (bClip) P.PushClip(C.X, C.Y, C.W, C.H);
		// 画像も本人なら同じだけ沈む/光る（C.Color を持たないので係数を直接掛ける）。
		FSlateDrawElement::MakeBox(P.Out, Layer + 1, Xf.PG(dx, dy, dw * Scale, dh * Scale), Brush, ESlateDrawEffect::None,
			FLinearColor(ImgL, ImgL, ImgL, C.Alpha));
		if (bClip) P.Out.PopClip();
	}

	// ---- 2026-10-12（rpg-themes/README.md §3）----
	void DrawKaedeGradient(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		if (C.GradColors.Num() < 2 || C.W <= 0.f || C.H <= 0.f) return;
		const FCmdXf Xf(P, C);
		const float LW = C.W * P.Scale, LH = C.H * P.Scale;
		TArray<FSlateGradientStop> Stops;
		for (int32 g = 0; g < C.GradColors.Num(); ++g)
		{
			const float p = C.GradPos.IsValidIndex(g) ? C.GradPos[g] : (float)g / (C.GradColors.Num() - 1);
			FLinearColor Lc = FLinearColor::FromSRGBColor(C.GradColors[g]);
			Lc.A *= C.Alpha;
			Stops.Add(FSlateGradientStop(FVector2f(p * LW, p * LH), Lc));
		}
		// Slate の Orient_Horizontal は上→下（色が縦に変わる）、Orient_Vertical は左→右
		FSlateDrawElement::MakeGradient(P.Out, Layer, Xf.PG(C.X, C.Y, LW, LH), Stops,
			C.bGradH ? Orient_Vertical : Orient_Horizontal, ESlateDrawEffect::None, FVector4f(C.Radius * P.Scale));
	}

	void DrawKaedePolygon(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		const int32 NP = C.Points.Num();
		if (NP < 3 || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer()) return;
		const FCmdXf Xf(P, C);
		// 耳を切る三角形分割（凹んだ形も・頂点は時計回りでも反時計回りでもよい）
		TArray<FVector2f> Pt;
		for (const FVector2f& Q : C.Points) Pt.Add(Xf.Point(Q));
		float Area = 0.f;
		for (int32 a = 0; a < NP; ++a) { const FVector2f& A = Pt[a]; const FVector2f& B = Pt[(a + 1) % NP]; Area += A.X * B.Y - B.X * A.Y; }
		const float Sign = Area >= 0.f ? 1.f : -1.f;
		TArray<int32> Idx; for (int32 a = 0; a < NP; ++a) Idx.Add(a);
		TArray<SlateIndex> Tri;
		auto Cross = [](const FVector2f& A, const FVector2f& B, const FVector2f& Cc) { return (B.X - A.X) * (Cc.Y - A.Y) - (B.Y - A.Y) * (Cc.X - A.X); };
		int32 Guard = 0;
		while (Idx.Num() > 3 && Guard++ < 1000)
		{
			bool bCut = false;
			for (int32 k = 0; k < Idx.Num(); ++k)
			{
				const int32 i0 = Idx[(k + Idx.Num() - 1) % Idx.Num()], i1 = Idx[k], i2 = Idx[(k + 1) % Idx.Num()];
				if (Cross(Pt[i0], Pt[i1], Pt[i2]) * Sign <= 0.f) continue;   // 凹んだ角は耳でない
				bool bInside = false;
				for (int32 m : Idx)
				{
					if (m == i0 || m == i1 || m == i2) continue;
					const float c1 = Cross(Pt[i0], Pt[i1], Pt[m]) * Sign, c2 = Cross(Pt[i1], Pt[i2], Pt[m]) * Sign, c3 = Cross(Pt[i2], Pt[i0], Pt[m]) * Sign;
					if (c1 > 0.f && c2 > 0.f && c3 > 0.f) { bInside = true; break; }
				}
				if (bInside) continue;
				Tri.Add((SlateIndex)i0); Tri.Add((SlateIndex)i1); Tri.Add((SlateIndex)i2);
				Idx.RemoveAt(k);
				bCut = true;
				break;
			}
			if (!bCut) break;
		}
		if (Idx.Num() == 3) { Tri.Add((SlateIndex)Idx[0]); Tri.Add((SlateIndex)Idx[1]); Tri.Add((SlateIndex)Idx[2]); }
		const FSlateRenderTransform& RT = P.Geo.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Verts;
		for (int32 a = 0; a < NP; ++a)
		{
			const FColor Vc = C.PointColors.IsValidIndex(a) ? C.PointColors[a] : C.Color;
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, P.ToLocal(Pt[a].X, Pt[a].Y), FVector2f(0.5f, 0.5f), Vc));
		}
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*P.White);
		FSlateDrawElement::MakeCustomVerts(P.Out, Layer, Handle, Verts, Tri, nullptr, 0, 0);
	}

	void DrawKaedeBlur(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C)
	{
		// 後ろをぼかす（SBackgroundBlur と同じ後処理。強さから核の大きさと縮小を決める）
		const FCmdXf Xf(P, C);
		const float Strength = FMath::Max(0.5f, C.Blur);
		int32 Kernel = FMath::RoundToInt(Strength * 3.f), Down = 0;
		if (Kernel > 9) { Down = Kernel >= 64 ? 4 : 2; Kernel /= Down; }
		if (Kernel % 2 == 0) ++Kernel;
		Kernel = FMath::Clamp(Kernel, 3, 255);
		const FPaintGeometry PG = Xf.PG(C.X, C.Y, C.W * P.Scale, C.H * P.Scale);
		const FVector2f Abs = P.Geo.GetAccumulatedRenderTransform().TransformVector(FVector2f(C.W * P.Scale, C.H * P.Scale));
		int32 RW = FMath::Max(1, FMath::RoundToInt(FMath::Abs(Abs.X))), RH = FMath::Max(1, FMath::RoundToInt(FMath::Abs(Abs.Y)));
		float Str = Strength;
		if (Down > 0) { RW = FMath::DivideAndRoundUp(RW, Down); RH = FMath::DivideAndRoundUp(RH, Down); Str /= Down; }
		FSlateDrawElement::MakePostProcessBlur(P.Out, Layer, PG, FVector4f((float)Kernel, Str, (float)RW, (float)RH), Down, FVector4f(0.f));
	}

	void DrawKaedeTextField(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C, const FString& Buffer)
	{
		// 非編集時は確定済みバッファを描く。空なら placeholder を薄色で。
		const float Scale = P.Scale;
		const bool bEmpty = Buffer.IsEmpty();
		const FString& Shown = bEmpty ? C.Text : Buffer;
		const FLinearColor Col = bEmpty ? FLinearColor(0.85f, 0.82f, 0.75f, 0.4f)
		                                : FLinearColor::FromSRGBColor(C.Color);
		// 文字の大きさは木の fontSize（TextFieldRect と同じ解決）。欄より長い文字列は欄の枠で切る
		//（2026-09-12: 手帳の検索欄の placeholder が隣の列へはみ出していた）。
		const float Px = C.FontPt > 0.f ? C.FontPt : (float)P.FontSize;
		const FSlateFontInfo Font = MakeKaedeUiFont(false, FMath::RoundToInt(Px * Scale), P.Family);
		FVector2f Pos = P.ToLocal(C.X, C.Y);
		if (FSlateApplication::IsInitialized() && !Shown.IsEmpty())
		{
			// 入力欄は打つ文字で行位置が動かないよう、基準の全角 1 字の字面で縦位置を決める
			// （編集時の SEditableText の配置 ArrangeChildren と同じ式。ずれると編集に入った瞬間に文字が跳ぶ）。
			Pos.Y = KaedeInkCenteredTextPos(TEXT("あ"), Font, Pos, C.H * Scale, false).Y;
		}
		P.PushClip(C.X, C.Y, C.W, C.H);
		FSlateDrawElement::MakeText(P.Out, Layer + 1, P.Geo.ToPaintGeometry(FVector2f(4000.f, Px * Scale), FSlateLayoutTransform(Pos)),
			Shown, Font, ESlateDrawEffect::None, Col);
		P.Out.PopClip();
	}

	void DrawKaedeScrollBar(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& Clip, float Offset, const FLinearColor& Thumb)
	{
		FKaedeScrollBar Bar;
		if (!ScrollBarGeometry(Clip.X, Clip.Y, Clip.W, Clip.H, Clip.ContentH, Offset, Bar)) return;
		const float Scale = P.Scale;
		FSlateDrawElement::MakeBox(P.Out, Layer, P.Box(Bar.TrackX, Bar.TrackY, Bar.TrackW * Scale, Bar.TrackH * Scale), P.White,
			ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.06f));
		FSlateDrawElement::MakeBox(P.Out, Layer, P.Box(Bar.TrackX, Bar.ThumbY, Bar.TrackW * Scale, Bar.ThumbH * Scale), P.White,
			ESlateDrawEffect::None, Thumb);
	}

	void DrawKaedeFocusRing(const FKaedePaintCtx& P, int32 Layer, const FKaedeHit& H, const SKaedeUiCanvas::FFocusStyle& S, const FSlateBrush* Cursor)
	{
		const float Scale = P.Scale;
		const float Th = FMath::Max(2.f, 3.f * Scale);
		if (Cursor)
		{
			const float CW = S.CursorW > 0.f ? S.CursorW : 32.f, CH = S.CursorH > 0.f ? S.CursorH : 24.f;
			const float CX = H.X + S.CursorDx, CY = H.Y + (H.H - CH) * 0.5f + S.CursorDy;
			FSlateDrawElement::MakeBox(P.Out, Layer + 2, P.Box(CX, CY, CW * Scale, CH * Scale), Cursor, ESlateDrawEffect::None, FLinearColor::White);
		}
		// 枠はカーソルの絵も文字・地の色替えも無いときだけ（従来の見た目）
		if (Cursor || S.bText || S.bFill) return;
		auto Ring = [&](float lx, float ly, float w, float h)
		{
			FSlateDrawElement::MakeBox(P.Out, Layer + 1, P.Box(lx, ly, w, h), P.White, ESlateDrawEffect::None, S.Ring);
		};
		const float X = H.X - 3, Y = H.Y - 3, W = H.W + 6, Hh = H.H + 6;
		Ring(X, Y, W * Scale, Th);
		Ring(X, Y + Hh, W * Scale, Th);
		Ring(X, Y, Th, Hh * Scale);
		Ring(X + W, Y, Th, Hh * Scale);
	}
}
