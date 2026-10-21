// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
using UnrealBuildTool;
using System.IO;

// MomijiUICore — MomijiUI（UI フレームワーク）の核。engine/ の下は UE に依存しない純 C++（RPG の CLI も同じ源を組み立てる）。
// 中身: core(UiNode,UiAnim) / ui(Interp,Easing,Tween) + nlohmann/json.hpp(ヘッダオンリー)。
// 公開ヘッダは json_fwd.hpp（前方宣言）だけを読み、json.hpp は解析する .cpp が include する。
// 公開の関数は Public/MomijiUICore.h（画面 JSON の解析・テーマ・prune・bind 等）。
// 設計: Docs/Guides/Working/product-split-design.md §3.2・§5.1（2026-10-14 KaedeCore から分けた）。
public class MomijiUICore : ModuleRules
{
	public MomijiUICore(ReadOnlyTargetRules Target) : base(Target)
	{
		// 素の C++ として扱う。PCH を使わない(共有 PCH の強制も明示 PCH も不要)。
		PCHUsage = PCHUsageMode.NoPCHs;

		// ヘッダオンリー大物(json.hpp)と同名 static の結合衝突を避けるため Unity を無効化。
		bUseUnity = false;

		// nlohmann::json はパース失敗時に throw するため例外を有効化。
		bEnableExceptions = true;

		// Core は素の C++。UE の IWYU ポリシーの対象外にする。
		IWYUSupport = IWYUSupport.None;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "engine"),     // "core/UiNode.hpp" / "ui/Interp.hpp" 等
			Path.Combine(ModuleDirectory, "ThirdParty"), // <nlohmann/json.hpp>・<nlohmann/json_fwd.hpp>（同じ 3.11.3）
		});

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
