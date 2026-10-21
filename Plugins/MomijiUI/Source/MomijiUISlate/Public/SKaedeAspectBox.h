// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SKaedeAspectBox.h — 子を「与えられた領域に収まる最大の、指定比率の矩形」へ中央に置き、外側を黒で塗る。
//
// 画面は固定の論理キャンバス（例 1920x1080）で作ってあり、窓の比率が違うとき（最大化で 1920x1017 など）に
// 実寸へ追従させると左右が崩れる（2026-10-02 ユーザー報告）。追従させず、比率を保った箱に収めて
// 余りは黒い帯にする（帯クアッドの 16:9 の教訓: 固定キャンバス＋レターボックスが正・実寸追従は禁止）。
// 子の Slate ウィジェットは箱の寸法で配置されるので、中の SKaedeUiCanvas のレターボックス式とも一致する。
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class MOMIJIUISLATE_API SKaedeAspectBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKaedeAspectBox)
		: _AspectW(16.f), _AspectH(9.f), _BarColor(FLinearColor::Black)
	{}
		SLATE_ARGUMENT(float, AspectW)          // 比率の横（論理幅をそのまま渡してよい）
		SLATE_ARGUMENT(float, AspectH)          // 比率の縦
		SLATE_ARGUMENT(FLinearColor, BarColor)  // 余りの帯の色
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// 子の矩形（この widget のローカル座標）。描画と当たり判定が同じ式を使う。
	void ContentRect(const FVector2f& Size, FVector2f& OutPos, FVector2f& OutSize) const;

protected:
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	float AspectW = 16.f;
	float AspectH = 9.f;
	FLinearColor BarColor = FLinearColor::Black;
};
