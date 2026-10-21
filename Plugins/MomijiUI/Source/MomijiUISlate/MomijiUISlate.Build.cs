// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
using UnrealBuildTool;

// MomijiUISlate — MomijiUI の Slate の描画器。UiNode 契約(MomijiUICore)を入力に、
// UiContext の描画/レイアウトを Slate(FSlateDrawElement)で再実装する。
// 2026-10-14 に KaedeUI から分けた（設計 Docs/Guides/Working/product-split-design.md §5.2）。ノベルの画面は KaedeVnUI。
public class MomijiUISlate : ModuleRules
{
	public MomijiUISlate(ReadOnlyTargetRules Target) : base(Target)
	{
		// 画面 JSON を解析する .cpp が nlohmann/json.hpp を include し、json::parse が throw しうる。
		bEnableExceptions = true;
		bUseUnity = false;
		PCHUsage = PCHUsageMode.NoPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",       // UTexture2D（KaedeLoadPngBrush）・-KaedeUiShot の窓と撮影
			"SlateCore",
			"Slate",
			"MomijiUICore",   // UiNode・LoadScreenTree ほか（画面 JSON）
		});

		// .cpp だけが使う（公開ヘッダに型が出ない）。
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",      // EKeys（キー・マウスの入力）
			"ImageWrapper",   // PNG デコード（KaedeLoadPngBrush）
		});

		// 同梱フォント（2026-10-02）。BIZ UDP ゴシック／明朝の Regular・Bold（SIL OFL 1.1・Google Fonts の公式リポジトリ）。
		// 以前は C:/Windows/Fonts の游ゴシック／游明朝を読んでいたが、配信版は日本語版 Windows 以外で豆腐になる。
		// pak に入れず loose file（NonUFS）で置く。読む側は KaedeUiFonts.cpp の FindJpFontFile。
		PrivateDependencyModuleNames.Add("Projects");   // IPluginManager（プラグインの Resources/ の場所）
		// FPlatformApplicationMisc::ClipboardCopy（文字の選択とコピー・2026-10-07）。Slate の依存から見えるのはヘッダだけで、リンクには明示が要る
		PrivateDependencyModuleNames.Add("ApplicationCore");
		foreach (var f in new[] { "BIZUDPGothic-Regular.ttf", "BIZUDPGothic-Bold.ttf",
		                          "BIZUDPMincho-Regular.ttf", "BIZUDPMincho-Bold.ttf",
		                          "OFL-BIZ-UDGothic.txt", "OFL-BIZ-UDMincho.txt",
		                          // RPG のテーマの書体（2026-10-12）: 手書き風・ドット
		                          "Yomogi-Regular.ttf", "OFL-Yomogi.txt", "DotGothic16-Regular.ttf", "OFL-DotGothic16.txt" })
		{
			RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/" + f, StagedFileType.NonUFS);
		}
	}
}
