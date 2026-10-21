// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// MomijiUISlate.cpp — モジュール登録と、画面 JSON を出す 2 つの起動引数。
//
// MomijiUISlate は「宣言的UI（UiNode）を Slate で描く層」＝ MomijiUI の UE 実装。ノベル（KaedeVnUI）も放置錬金も ADV も RPG もここを共有する。
// 2026-10-14 に KaedeUI から分けた（設計 Docs/Guides/Working/product-split-design.md §5.2）。
// 仕様 Docs/Guides/Working/momiji-ui-framework.md・使い方 Docs/Guides/Manuals/momiji-ui-manual.md。
//
// -KaedeUiShot=<画面 JSON のパス（プロジェクト相対か絶対）>（2026-10-03）:
//   ゲームの窓が出来たら、その JSON を 1280×720 のキャンバスに描いて最前面に重ね、撮って終わる。
//   MomijiUI の照合（Scripts/momiji-ui-conformance.ps1 -Shots）がブラウザの絵と並べるために使う。
//   JSON の "bind"（strings / vars / lists）があれば差し込んでから描く（照合の自動テストと同じ読み方）。
//   出力: Saved/Screenshots/<Platform>/MomijiUI.png。
// -KaedeUiExample=counter（2026-10-03）:
//   説明書の見本（SMomijiUICounterExample）をゲームの窓に出す。触って確かめる用（終わらない）。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28 ／ 起動引数: Claude Opus 5.5 — 2026-10-03
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "MomijiUICore.h"
#include "SKaedeUiCanvas.h"
#include "Examples/SMomijiUICounterExample.h"
#include "MomijiUIProjectPaths.h"   // 見本の画面の置き場（プラグインの Examples/）
#include "KaedeUiResources.h"      // 書体とブラシの控えを捨てる（ShutdownModule）
#include <nlohmann/json.hpp>

DEFINE_LOG_CATEGORY_STATIC(LogKaedeUiShot, Log, All);

namespace
{
	struct FUiShot
	{
		FString Path;                      // -KaedeUiShot（空なら見本を出す）
		FString Example;                   // -KaedeUiExample
		TSharedPtr<SWidget> Widget;
		double Started = -1.0;
		bool bRequested = false;
	};

	bool TickShot(float, TSharedPtr<FUiShot> Shot)
	{
		if (!GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return true;
		const double Now = FPlatformTime::Seconds();
		if (!Shot->Widget.IsValid())
		{
			if (!Shot->Example.IsEmpty())
			{
				if (Shot->Example != TEXT("counter")) { UE_LOG(LogKaedeUiShot, Error, TEXT("[ui-example] 知らない見本: %s（counter だけ）"), *Shot->Example); return false; }
				Shot->Widget = SNew(SMomijiUICounterExample).ScreenPath(MomijiUI::PluginDir() / TEXT("Examples/counter.json"));
				GEngine->GameViewport->AddViewportWidgetContent(Shot->Widget.ToSharedRef(), 10000);
				UE_LOG(LogKaedeUiShot, Display, TEXT("[ui-example] showing %s"), *Shot->Example);
				return false;   // 見本は出したら終わり（触る用）
			}
			FString Json;
			if (!FFileHelper::LoadFileToString(Json, *Shot->Path)) { UE_LOG(LogKaedeUiShot, Error, TEXT("[ui-shot] 読めない: %s"), *Shot->Path); FPlatformMisc::RequestExit(false); return false; }
			// 1 回だけ parse し、木と bind を同じ JSON から読む（照合の自動テスト MomijiUI.MomijiUIConformance と同じ読み方）
			const nlohmann::json J = nlohmann::json::parse(TCHAR_TO_UTF8(*Json), nullptr, false);
			if (!J.is_object()) { UE_LOG(LogKaedeUiShot, Error, TEXT("[ui-shot] JSON でない: %s"), *Shot->Path); FPlatformMisc::RequestExit(false); return false; }
			const auto Bind = J.find("bind");
			TSharedRef<SKaedeUiCanvas> Canvas = SNew(SKaedeUiCanvas);
			KaedeUI::PresentScreen(*Canvas, KaedeCore::ScreenTreeFromJson(J),
				Bind != J.end() ? KaedeUI::ReadScreenValues(*Bind) : KaedeUI::FKaedeScreenValues{},
				KaedeCore::BuiltinTheme("midnight"),   // ブラウザの照合と同じテーマ
				FLinearColor::FromSRGBColor(FColor(0x0e, 0x11, 0x16)));
			Shot->Widget = Canvas;
			GEngine->GameViewport->AddViewportWidgetContent(Canvas, 10000);   // 何が下にあっても最前面
			Shot->Started = Now;
			UE_LOG(LogKaedeUiShot, Display, TEXT("[ui-shot] showing %s"), *Shot->Path);
			return true;
		}
		// 画像の読み込みと最初の数フレームを待ってから撮り、書き出しを待って終わる
		if (!Shot->bRequested && Now - Shot->Started > 2.5)
		{
			Shot->bRequested = true;
			FScreenshotRequest::RequestScreenshot(TEXT("MomijiUI"), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogKaedeUiShot, Display, TEXT("[ui-shot] screenshot requested"));
			return true;
		}
		if (Shot->bRequested && Now - Shot->Started > 6.0)
		{
			UE_LOG(LogKaedeUiShot, Display, TEXT("[ui-shot] done -> exit"));
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}
}

class FMomijiUISlateModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		TSharedPtr<FUiShot> Shot = MakeShared<FUiShot>();
		FParse::Value(FCommandLine::Get(), TEXT("KaedeUiShot="), Shot->Path);
		FParse::Value(FCommandLine::Get(), TEXT("KaedeUiExample="), Shot->Example);
		if (Shot->Path.IsEmpty() && Shot->Example.IsEmpty()) return;
		if (!Shot->Path.IsEmpty() && FPaths::IsRelative(Shot->Path)) Shot->Path = FPaths::ProjectDir() / Shot->Path;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Shot](float Dt) { return TickShot(Dt, Shot); }));
	}

	virtual void ShutdownModule() override
	{
		KaedeUI::ReleaseKaedeUiFonts();
		KaedeUI::ReleaseKaedeUiBrushes();
	}
};

IMPLEMENT_MODULE(FMomijiUISlateModule, MomijiUISlate);
