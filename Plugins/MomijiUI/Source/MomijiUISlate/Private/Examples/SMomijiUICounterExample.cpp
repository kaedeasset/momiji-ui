// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SMomijiUICounterExample.cpp — 説明書の見本（宣言の .h を参照）。
#include "SMomijiUICounterExample.h"
#include "MomijiUICore.h"   // KaedeCore::LoadScreenTree / BuiltinTheme
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMomijiUIExample, Log, All);

void SMomijiUICounterExample::Construct(const FArguments& InArgs)
{
	// 1. 画面 JSON を読む（一度だけ）
	FString Json;
	if (FFileHelper::LoadFileToString(Json, *InArgs._ScreenPath))
		Tree = KaedeCore::LoadScreenTree(TCHAR_TO_UTF8(*Json));   // JSON として読めなければ LoadScreenTree がエラーをログに出し、空の木
	else
		UE_LOG(LogMomijiUIExample, Error, TEXT("[ui-example] 画面 JSON のファイルが読めない: %s"), *InArgs._ScreenPath);
	Theme = KaedeCore::BuiltinTheme("midnight");

	// 2. キャンバスを置き、押された action を受ける
	ChildSlot[ SAssignNew(Canvas, SKaedeUiCanvas) ];
	Canvas->SetOnAction([this](const FString& Action, float /*RelX*/) { HandleAction(Action); });

	Refresh();
}

void SMomijiUICounterExample::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	Motion.update((double)DeltaTime * 1000.0);   // 動き（anim）を進める。描くのはキャンバスが毎フレーム
}

void SMomijiUICounterExample::HandleAction(const FString& Action)
{
	// 3. action を受けて状態を変える（action の意味は host が決める）
	if (Action == TEXT("count:add")) { ++Count; }
	else if (Action == TEXT("count:reset")) { Count = 0; }
	else if (Action == TEXT("name:add"))
	{
		// 文字欄（bindVar "name"）の中身はキャンバスが持っている
		const FString Name = Canvas->GetTextInputValue(TEXT("name")).TrimStartAndEnd();
		if (!Name.IsEmpty()) { Names.Add(Name); }
		Canvas->SetTextInputValue(TEXT("name"), FString());
	}
	Refresh();   // 4. 描き直す
}

void SMomijiUICounterExample::Refresh()
{
	// 状態から「値」を作る。部品は組み立てない（形と書式は JSON が持つ）
	KaedeUI::FKaedeScreenValues V;
	V.Strings["title"] = TCHAR_TO_UTF8(TEXT("momiji-ui の見本"));
	V.Vars["count"] = Count;   // 条件式（enabledIf / showIf）にも {count} にも使える
	std::vector<kaedevn::ui::ItemScope> Rows;
	for (const FString& N : Names) { Rows.push_back({ {"kind", "name"}, {"text", TCHAR_TO_UTF8(*N)} }); }
	if (Rows.empty()) { Rows.push_back({ {"kind", "empty"} }); }
	V.Lists["names"] = Rows;

	KaedeUI::PresentScreen(*Canvas, Tree, V, Theme, FLinearColor::FromSRGBColor(FColor(0x0e, 0x11, 0x16)),
		&Motion, TEXT("counter"));   // 動きを使う画面は Motion と画面の名前も渡す
}
