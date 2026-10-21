// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SMomijiUICounterExample — MomijiUI の利用者向け説明書（Docs/Guides/Manuals/momiji-ui-manual.md §4）の見本。
// 画面は counter.json（MomijiUI のプラグインの Examples/counter.json。ブラウザの見本 Tools/momiji-ui/examples/counter/ と同じファイル）。
// 画面 JSON のパスは呼ぶ側が渡す（MomijiUI は作品の置き場を持たない）。
// 説明書に載せたコードはこのファイルと .cpp がそのまま正。自動テスト MomijiUI.ManualExample が動かして確かめる。
// ゲームで触るには起動引数 -KaedeUiExample=counter（MomijiUISlate.cpp）。
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SKaedeUiCanvas.h"
#include "ui/Tween.hpp"   // kaedevn::ui::UiMotionState（anim を使う画面）

class SMomijiUICounterExample : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMomijiUICounterExample) {}
		SLATE_ARGUMENT(FString, ScreenPath)   // counter.json の絶対パス
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;

	// 押された action を受ける（キャンバスの SetOnAction から呼ばれる。テストからも呼ぶ）
	void HandleAction(const FString& Action);

	int32 GetCount() const { return Count; }
	const TArray<FString>& GetNames() const { return Names; }
	TSharedPtr<SKaedeUiCanvas> GetCanvas() const { return Canvas; }
	const kaedevn::ui::UiMotionState& GetMotion() const { return Motion; }

private:
	void Refresh();   // 状態から値を作って描き直す

	kaedevn::ui::UiNode Tree;          // 読み込んだ画面（書き換えない）
	kaedevn::ui::UiTheme Theme;
	TSharedPtr<SKaedeUiCanvas> Canvas;
	kaedevn::ui::UiMotionState Motion; // 動き（anim）の状態。host が持ち、Tick で進める
	int32 Count = 0;                   // ← 画面の状態はここ（host）が持つ
	TArray<FString> Names;
};
