// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SKaedeAspectBox.cpp — 比率を保った箱に子を収め、余りを黒で塗る（説明はヘッダ）。
#include "SKaedeAspectBox.h"

#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SKaedeAspectBox::Construct(const FArguments& InArgs)
{
	AspectW = FMath::Max(1.f, InArgs._AspectW);
	AspectH = FMath::Max(1.f, InArgs._AspectH);
	BarColor = InArgs._BarColor;
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SKaedeAspectBox::ContentRect(const FVector2f& Size, FVector2f& OutPos, FVector2f& OutSize) const
{
	// 収まる最大の比率矩形（SKaedeUiCanvas のレターボックスと同じ min 式）
	const float Scale = FMath::Min(Size.X / AspectW, Size.Y / AspectH);
	OutSize = FVector2f(AspectW * Scale, AspectH * Scale);
	OutPos = (Size - OutSize) * 0.5f;
}

void SKaedeAspectBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget> Child = ChildSlot.GetWidget();
	if (!ArrangedChildren.Accepts(Child->GetVisibility())) { return; }
	FVector2f Pos, Size;
	ContentRect(AllottedGeometry.GetLocalSize(), Pos, Size);
	ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Child, Pos, Size));
}

int32 SKaedeAspectBox::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 余りの帯。左右または上下のどちらか一方しか出ない（min 式なので片方は 0）。
	const FVector2f Full = AllottedGeometry.GetLocalSize();
	FVector2f Pos, Size;
	ContentRect(Full, Pos, Size);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	auto Bar = [&](const FVector2f& P, const FVector2f& S)
	{
		if (S.X <= 0.5f || S.Y <= 0.5f) { return; }
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(S, FSlateLayoutTransform(P)), White, ESlateDrawEffect::None, BarColor);
	};
	Bar(FVector2f(0.f, 0.f), FVector2f(Pos.X, Full.Y));                              // 左
	Bar(FVector2f(Pos.X + Size.X, 0.f), FVector2f(Full.X - Pos.X - Size.X, Full.Y));  // 右
	Bar(FVector2f(0.f, 0.f), FVector2f(Full.X, Pos.Y));                              // 上
	Bar(FVector2f(0.f, Pos.Y + Size.Y), FVector2f(Full.X, Full.Y - Pos.Y - Size.Y));  // 下
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}
