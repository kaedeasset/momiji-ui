// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// MomijiUIProjectPaths.h — 試験と見本の起動（-KaedeUiExample）が読む、このプロジェクトの画面 JSON の置き場と、MomijiUI のプラグインの置き場（同梱フォントの置き場も）。
//
// MomijiUI は作品の置き場を自分で持たない（部品は画面 JSON のパスを呼ぶ側から受ける）。プロジェクトの画面を読む
// 試験と見本の起動だけは、プロジェクトの設定（DefaultGame.ini の [MomijiUI] ScreenRoot。プロジェクトの直下からの相対か絶対）
// から置き場を求める。設定が無ければ空を返す（試験は「読めない」で落ちる）。分割 S4（2026-10-14）。
// 照合のフィクスチャ（Fixtures/）と見本（Examples/counter.json）はプラグインの中にある（分割 S5・2026-10-14）。PluginDir() から求める。
#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"

namespace MomijiUI
{
	inline FString ProjectScreenRoot()
	{
		FString Root;
		if (!GConfig || !GConfig->GetString(TEXT("MomijiUI"), TEXT("ScreenRoot"), Root, GGameIni) || Root.IsEmpty()) { return FString(); }
		if (FPaths::IsRelative(Root)) { Root = FPaths::ProjectDir() / Root; }
		FPaths::NormalizeDirectoryName(Root);
		return Root;
	}

	// MomijiUISlate を持つプラグイン（MomijiUI）の置き場。プラグイン名で探さない（設計 product-split-design.md §5.6）。探すのはここ 1 か所
	inline FString PluginDir()
	{
		for (const TSharedRef<IPlugin>& P : IPluginManager::Get().GetEnabledPlugins())
			for (const FModuleDescriptor& M : P->GetDescriptor().Modules)
				if (M.Name == TEXT("MomijiUISlate")) return FPaths::ConvertRelativePathToFull(P->GetBaseDir());
		return FString();
	}
}