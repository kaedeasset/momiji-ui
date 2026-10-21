// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiBrushes.cpp — 角丸ブラシの控えと PNG → ブラシ（宣言は SKaedeUiCanvas.h の KaedeUI 名前空間）。
// 2026-10-18 に SKaedeUiCanvas.cpp から分けた（Issue #73）。
#include "SKaedeUiCanvas.h"
#include "KaedeUiResources.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"   // FTexture2DMipMap（GetPlatformData()->Mips。unity 非依存の自己完結に必要）
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

namespace
{
	// MakeBox は brush ポインタを保持するため、作ったブラシはモジュールが終わるまで控える（ReleaseKaedeUiBrushes）。
	// 枠ブラシは色ごとに 1 つ。使う色数は有限（テーマ色+固定色）なので、描画中に捨てる仕組みは持たない
	TMap<int32, TSharedPtr<FSlateBrush>> GFillBrushes;
	TMap<uint64, TSharedPtr<FSlateBrush>> GOutlineBrushes;
}

namespace KaedeUI
{
	void ReleaseKaedeUiBrushes()
	{
		GFillBrushes.Reset();
		GOutlineBrushes.Reset();
	}

	const FSlateBrush* GetKaedeRoundedFillBrush(float RadiusPx)
	{
		// 白塗り+角丸のみ（着色は MakeBox の Tint）。半径は整数量子化してキャッシュ。
		const int32 Key = FMath::Clamp(FMath::RoundToInt(RadiusPx), 1, 128);
		if (const TSharedPtr<FSlateBrush>* Found = GFillBrushes.Find(Key)) return Found->Get();
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, (float)Key);
		GFillBrushes.Add(Key, Brush);
		return Brush.Get();
	}

	const FSlateBrush* GetKaedeRoundedOutlineBrush(float RadiusPx, const FLinearColor& Color, float Width)
	{
		// 枠色はブラシに焼き込む（Rounded Box の Outline 色は要素 Tint が乗らないため）。
		const int32 R = FMath::Clamp(FMath::RoundToInt(RadiusPx), 1, 128);
		const int32 W = FMath::Clamp(FMath::RoundToInt(Width), 1, 16);
		const FColor C = Color.ToFColor(/*bSRGB*/ true);
		const uint64 Key = ((uint64)R << 48) | ((uint64)W << 40) |
			((uint64)C.R << 32) | ((uint64)C.G << 24) | ((uint64)C.B << 16) | ((uint64)C.A << 8);
		if (const TSharedPtr<FSlateBrush>* Found = GOutlineBrushes.Find(Key)) return Found->Get();
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateRoundedBoxBrush>(
			FLinearColor::Transparent, (float)R, Color, (float)W);
		GOutlineBrushes.Add(Key, Brush);
		return Brush.Get();
	}

	TSharedPtr<FSlateBrush> KaedeLoadPngBrush(const FString& FilePath, UTexture2D*& OutTexture, bool bNearest)
	{
		OutTexture = nullptr;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath)) return nullptr;

		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		const TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num())) return nullptr;

		TArray<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) return nullptr;
		const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();

		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex) return nullptr;
		void* Mip = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Mip, Raw.GetData(), Raw.Num());
		Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
		Tex->SRGB = true;
		if (bNearest) { Tex->Filter = TF_Nearest; Tex->LODGroup = TEXTUREGROUP_Pixels2D; }   // ドット絵（2026-10-12）
		Tex->UpdateResource();

		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Tex);
		Brush->ImageSize = FVector2D(W, H);
		Brush->DrawAs = ESlateBrushDrawType::Image;
		OutTexture = Tex;
		return Brush;
	}
}
