// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiFonts.cpp — MomijiUI の書体の解決と字の計測（宣言は SKaedeUiCanvas.h の KaedeUI 名前空間）。
//
// 系統は 4 つ:
//   Gothic（既定・BIZ UDP ゴシック）
//   Mincho（BIZ UDP 明朝）
//   Hand（Yomogi）・Dot（DotGothic16）… RPG のテーマ（2026-10-12）
// 以前は同じ解決処理が3ファイルに複製されていた。増やすときはここ1箇所。
// 2026-10-18 に SKaedeUiCanvas.cpp から分けた（Issue #73）。
#include "SKaedeUiCanvas.h"
#include "KaedeUiResources.h"
#include "MomijiUIProjectPaths.h"   // MomijiUI::PluginDir（同梱フォントの置き場＝<プラグイン>/Resources/Fonts）

#include "Fonts/CompositeFont.h"
#include "Fonts/FontCache.h"     // 字面の測定（MeasureKaedeTextInk）: 字形ごとのアトラス位置と寸法
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogKaedeUiFonts, Log, All);

namespace
{
	// 控え（系統 × ウェイト）。合成フォント生成は重いので使い回す。探して無かった組は控え、
	// 以後は探さずエンジン既定に落とす（ログは組ごとに 1 回。文字を描くたびに探し直さない）
	struct FFontCache
	{
		FString FontDir;
		TSharedPtr<FCompositeFont> Fonts[KaedeUI::KaedeFontFamilyCount][2];
		bool bSearched[KaedeUI::KaedeFontFamilyCount][2] = {};
	};
	FFontCache GFonts;

	// 同梱フォント（2026-10-02）。プラグインの Resources/Fonts にある BIZ UDP ゴシック／明朝（SIL OFL 1.1）を
	// 系統 × ウェイトで 1 つに決めて読む。MomijiUISlate.Build.cs が配信物へ loose file として積む。
	// 以前は C:/Windows/Fonts の游ゴシック／游明朝（→メイリオ→MS）を探していたが、配信版が日本語版
	// Windows 以外で豆腐になるので、OS のフォントは見ない（候補探しも残さない。1 か所で決める）。
	// 無ければ空を返し、呼び出し側がエンジン既定の書体（英数のみ）に落ちる＝ログで気づける。
	FString FindJpFontFile(bool bBold, KaedeUI::EKaedeFontFamily Family)
	{
		using EF = KaedeUI::EKaedeFontFamily;
		if (GFonts.FontDir.IsEmpty())
		{
			const FString Plugin = MomijiUI::PluginDir();
			GFonts.FontDir = Plugin.IsEmpty() ? FString() : (Plugin / TEXT("Resources/Fonts"));
		}
		const TCHAR* File = Family == EF::Mincho ? (bBold ? TEXT("BIZUDPMincho-Bold.ttf") : TEXT("BIZUDPMincho-Regular.ttf"))
		                  : Family == EF::Hand ? TEXT("Yomogi-Regular.ttf")         // 手書き風（太字の版は無い）
		                  : Family == EF::Dot ? TEXT("DotGothic16-Regular.ttf")     // ドット書体（太字の版は無い）
		                                         : (bBold ? TEXT("BIZUDPGothic-Bold.ttf") : TEXT("BIZUDPGothic-Regular.ttf"));
		const FString Path = GFonts.FontDir.IsEmpty() ? FString() : (GFonts.FontDir / File);
		if (!Path.IsEmpty() && FPaths::FileExists(Path)) return Path;
		UE_LOG(LogKaedeUiFonts, Error, TEXT("[ui] bundled font missing: %s (dir=%s) — falling back to the engine default font"), File, *GFonts.FontDir);
		return FString();
	}
}

namespace KaedeUI
{
	void ReleaseKaedeUiFonts() { GFonts = FFontCache(); }

	FSlateFontInfo MakeKaedeUiFont(bool bBold, int32 SizePx, EKaedeFontFamily Family)
	{
		const int32 F = FMath::Clamp((int32)Family, 0, KaedeFontFamilyCount - 1);
		TSharedPtr<FCompositeFont>& Cache = GFonts.Fonts[F][bBold ? 1 : 0];
		bool& bTried = GFonts.bSearched[F][bBold ? 1 : 0];
		if (!Cache.IsValid() && !bTried)
		{
			bTried = true;
			const FString Path = FindJpFontFile(bBold, Family);
			if (!Path.IsEmpty())
				Cache = MakeShared<FStandaloneCompositeFont>(NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		if (Cache.IsValid()) return FSlateFontInfo(Cache, SizePx);
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", SizePx);
	}

	FKaedeTextInk MeasureKaedeTextInk(const FString& Text, const FSlateFontInfo& Font)
	{
		// Slate が文字を描くときと同じ経路（字形の整形→アトラス）で、字形ごとの実寸と位置を足し合わせる。
		// 描画側（FSlateElementBatcher）の式: 基線 = 上端 + 最大高 + 基線オフセット、
		// 字形の左上 = (X + 水平オフセット + XOffset, 基線 - 垂直オフセット + YOffset)、大きさ = USize × VSize。
		FKaedeTextInk R;
		if (!FSlateApplication::IsInitialized() || Text.IsEmpty()) return R;
		const TSharedRef<FSlateFontCache> Cache = FSlateApplication::Get().GetRenderer()->GetFontCache();
		const FShapedGlyphSequenceRef Shaped = Cache->ShapeUnidirectionalText(
			Text, Font, 1.f, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
		const float BaselineY = (float)Shaped->GetMaxTextHeight() + (float)Shaped->GetTextBaseline();
		float X = 0.f;
		bool bAny = false;
		FVector2f Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
		for (const FShapedGlyphEntry& G : Shaped->GetGlyphsToRender())
		{
			if (G.bIsVisible)
			{
				const FShapedGlyphFontAtlasData A = Cache->GetShapedGlyphFontAtlasData(G, FFontOutlineSettings::NoOutline);
				if (A.Valid && A.USize > 0 && A.VSize > 0)
				{
					const float Gx = X + (float)A.HorizontalOffset + (float)G.XOffset;
					const float Gy = BaselineY - (float)A.VerticalOffset + (float)G.YOffset;
					Mn.X = FMath::Min(Mn.X, Gx);                  Mn.Y = FMath::Min(Mn.Y, Gy);
					Mx.X = FMath::Max(Mx.X, Gx + (float)A.USize); Mx.Y = FMath::Max(Mx.Y, Gy + (float)A.VSize);
					bAny = true;
				}
			}
			X += (float)G.XAdvance;
		}
		R.Advance = X;
		if (bAny) { R.Min = Mn; R.Max = Mx; R.bValid = true; }
		return R;
	}

	FVector2f KaedeInkCenteredTextPos(const FString& Text, const FSlateFontInfo& Font,
	                                   FVector2f Pos, float BoxH, bool bCenterX)
	{
		const FKaedeTextInk Ink = MeasureKaedeTextInk(Text, Font);
		if (!Ink.bValid)
		{
			if (FSlateApplication::IsInitialized())
			{
				const float H = (float)FSlateApplication::Get().GetRenderer()
					->GetFontMeasureService()->Measure(Text, Font).Y;
				Pos.Y += (BoxH - H) * 0.5f;
			}
			return Pos;
		}
		Pos.Y += (BoxH - (Ink.Max.Y - Ink.Min.Y)) * 0.5f - Ink.Min.Y;
		if (bCenterX) Pos.X += (Ink.Advance - (Ink.Max.X - Ink.Min.X)) * 0.5f - Ink.Min.X;
		return Pos;
	}

	FKaedeMeasureFn MakeSlateMeasure(int32 DefaultPx, EKaedeFontFamily Family)
	{
		return [DefaultPx, Family](const FString& Text, bool bBold, float FontPt) -> float
		{
			if (!FSlateApplication::IsInitialized()) return (float)Text.Len() * (float)DefaultPx;
			const int32 Px = FontPt > 0.f ? FMath::RoundToInt(FontPt) : DefaultPx;
			return (float)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
				->Measure(Text, MakeKaedeUiFont(bBold, Px, Family)).X;
		};
	}
}
