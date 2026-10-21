// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SKaedeMomijiUIPreview.cpp — 画面JSON（Game/ui/*.json）のプレビュータブ。
//
// KaedeVnPreview がノベル本編を映すのに対し、こちらは**オーバーレイUI**
// （タイトル/メニュー/設定/セーブ…）の JSON を編集しながら確認するためのもの。
// テーマ切替・PC/タッチ切替・showIf/hideIf・タブ/Slider/Toggle の操作を備える。
//
// 論理サイズはパリティ基準の 1280x720 のまま。JSON 自体がその基準で書かれており、
// 本編ではランタイムが ScaleUiTree で拡大する。ここで 1920 にすると拡大が効かず、
// 要素が画面比で小さく見えてしまう。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "KaedeMomijiUIPreview.h"
#include "KaedeUiPainter.h"        // SeedVarsFromTree / FKaedeDrawCmd / FKaedeHit（UiNode 同梱）
#include "SKaedeUiCanvas.h"        // 共有キャンバス（描画/ヒット/ドラッグ）+ PresentScreen
#include "MomijiUICore.h"     // KaedeCore::LoadScreenTree / LoadTheme / BuiltinTheme / PruneScreenTree
#include <nlohmann/json.hpp>

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/FileManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <string>
#include <unordered_map>

#define LOCTEXT_NAMESPACE "SKaedeMomijiUIPreview"

// MomijiUI 画面JSON を PresentScreen で描くプレビュー。
// 設定画面（Phase 0/1）検証用に、テーマ切替・PC/タッチ切替・showIf/hideIf・
// タブ/Slider/Toggle の操作を備える（変数はプレビュー内メモリのみ。永続化は Phase 1 後半）。
class SKaedeMomijiUIPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKaedeMomijiUIPreview) {}
		SLATE_ARGUMENT(FString, ScreenPath)
		SLATE_ARGUMENT(FString, ThemePath)
		SLATE_ARGUMENT(FString, RootDir)
		SLATE_ARGUMENT(FString, ListFilter)
		SLATE_ARGUMENT(bool, bShowStyleControls)
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnTitleChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ScreenPath = InArgs._ScreenPath;
		ThemePath = InArgs._ThemePath;
		RootDir = InArgs._RootDir;
		ListFilter = InArgs._ListFilter;
		bShowStyleControls = InArgs._bShowStyleControls;
		OnTitleChanged = InArgs._OnTitleChanged;
		ScreenDir = FPaths::GetPath(ScreenPath);
		CurrentScreen = FPaths::GetBaseFilename(ScreenPath);

		auto ThemeButton = [this](const TCHAR* Name) -> TSharedRef<SWidget>
		{
			const FString NameStr(Name);
			return SNew(SButton)
				.Text(FText::FromString(NameStr))
				.OnClicked_Lambda([this, NameStr]() { SetTheme(NameStr); return FReply::Handled(); });
		};
		// 画面は固定4枚ではなく Game/**/ui/*.json を走査して選ばせる。
		// 生成物（攻略地図など）を出した側が C++ を触らずにタブへ載せられるようにするため。
		RefreshScreenList();
		CurrentLabel = RelLabelFor(ScreenPath);

		// ツールバーは宣言チェーンではなく手で組む。スタイル系（テーマ／端末）を
		// 条件で外す必要があるため——攻略地図のように「中身を読むだけ」のタブでは、
		// 見た目を作り込む道具が並んでいると何を触ればいいのか分からなくなる。
		TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox);
		Toolbar->AddSlot().AutoWidth().Padding(0,0,8,0)
		[ SNew(SButton).Text(LOCTEXT("Reload","再読込")).OnClicked(this,&SKaedeMomijiUIPreview::OnReload) ];
		Toolbar->AddSlot().AutoWidth().Padding(0,0,2,0).VAlign(VAlign_Center)
		[ SNew(STextBlock).Text(LOCTEXT("ScreenLabel","画面:")) ];
		Toolbar->AddSlot().AutoWidth().Padding(2,0,8,0)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&ScreenList)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
			{ return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString())); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
			{ if (In.IsValid()) SelectScreen(*In); })
			[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(CurrentLabel); }) ]
		];
		if (bShowStyleControls)
		{
			Toolbar->AddSlot().AutoWidth().Padding(0,0,2,0).VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(LOCTEXT("ThemeLabel","テーマ:")) ];
			Toolbar->AddSlot().AutoWidth().Padding(2,0) [ ThemeButton(TEXT("atelier")) ];
			Toolbar->AddSlot().AutoWidth().Padding(2,0) [ ThemeButton(TEXT("midnight")) ];
			Toolbar->AddSlot().AutoWidth().Padding(2,0,8,0) [ ThemeButton(TEXT("sakura")) ];
			Toolbar->AddSlot().AutoWidth().Padding(0,0,2,0).VAlign(VAlign_Center)
			[ SNew(STextBlock).Text(LOCTEXT("DeviceLabel","端末:")) ];
			Toolbar->AddSlot().AutoWidth().Padding(2,0)
			[ SNew(SButton).Text(LOCTEXT("DevicePC","PC"))
				.OnClicked_Lambda([this]() { SetTouch(false); return FReply::Handled(); }) ];
			Toolbar->AddSlot().AutoWidth().Padding(2,0,8,0)
			[ SNew(SButton).Text(LOCTEXT("DeviceTouch","タッチ"))
				.OnClicked_Lambda([this]() { SetTouch(true); return FReply::Handled(); }) ];
		}
		Toolbar->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
		[ SAssignNew(StatusBlock, STextBlock) ];

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(6.f)
			[ Toolbar ]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(6.f,0.f,6.f,6.f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2.f)
				[ SAssignNew(Canvas, SKaedeUiCanvas) ]
			]
		];

		Canvas->SetOnAction([this](const FString& Action, float RelX) { HandleAction(Action, RelX); });
		Load();
	}

	// ライブプレビュー: 開いている画面JSON が書き換わったら自動で読み直す。
	//
	// IDirectoryWatcher を使わないのは、あれがエディタ専用モジュールで、KaedeUI は
	// ゲーム側でも使う実行時モジュールだから。1ファイルのタイムスタンプを覗くだけで
	// 用は足りるので、パッケージに響く依存を増やさない方を選んだ。
	virtual void Tick(const FGeometry&, const double InCurrentTime, const float) override
	{
		if (InCurrentTime - LastPollTime < kPollSeconds) return;
		LastPollTime = InCurrentTime;
		if (CurrentScreen.IsEmpty()) return;

		const FString Path = ScreenDir / CurrentScreen + TEXT(".json");
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Path);
		if (Stamp == FDateTime::MinValue()) return;      // 書き換え中に一瞬消えることがある
		if (Stamp == LastWriteTime) return;

		// **Vars は消さない。** 値を触りながら JSON を直しているときに、保存のたびに
		// スライダやタブ位置が既定へ戻ると編集にならない（手動の「再読込」は従来どおり戻す）。
		Load();   // 基準時刻の取り直しは Load() が行う（画面切替でも同じ経路を通るため）
		SetStatus(FString::Printf(TEXT("%s — 自動再読込 %s"),
			*CurrentLabel, *FDateTime::Now().ToString(TEXT("%H:%M:%S"))));
	}

private:
	static constexpr double kPollSeconds = 0.5;   // 体感で即時・1ファイルなので負荷は無視できる
	double LastPollTime = 0.0;
	FDateTime LastWriteTime = FDateTime::MinValue();

	// 再読込 = 変数（設定値/タブ位置）も既定へ戻すフルリセット。goto: 遷移では Vars を保持する。
	// 一覧も取り直す（生成器が新しい画面を吐いた直後に、タブを開き直さずに拾えるように）。
	FReply OnReload() { Vars.clear(); RefreshScreenList(); Load(); return FReply::Handled(); }

	// 呼ぶ側が渡した置き場（RootDir）以下の "…/ui/<name>.json" を全部集める。ui（共有）も adv/ui（作品）も同じ扱い。
	// MomijiUI は作品の置き場を自分で持たない（KaedeVN なら KaedeVn::GetWorkRoot() を渡す）。

	// フルパス → 一覧に出す短い名前（例 "ui/title" / "adv/ui/route-map"）。
	FString RelLabelFor(const FString& FullPath) const
	{
		FString Rel = FullPath;
		FPaths::MakePathRelativeTo(Rel, *(RootDir / TEXT("")));
		const FString Dir = FPaths::GetPath(Rel);
		return Dir.IsEmpty() ? FPaths::GetBaseFilename(Rel) : Dir / FPaths::GetBaseFilename(Rel);
	}

	void RefreshScreenList()
	{
		ScreenList.Empty();
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *RootDir, TEXT("*.json"), /*bFiles=*/true, /*bDirs=*/false);
		for (const FString& F : Files)
		{
			// 画面JSONの置き場は "ui" フォルダだけ、という約束にしている。
			// これが無いと intents/scripts など数百のデータ JSON まで一覧に出てしまう。
			if (FPaths::GetCleanFilename(FPaths::GetPath(F)) != TEXT("ui")) continue;
			const FString Label = RelLabelFor(F);
			// 用途を絞ったタブでは関係ない画面を出さない（攻略地図タブに title/menu が並ばないように）。
			if (!ListFilter.IsEmpty() && !Label.Contains(ListFilter)) continue;
			ScreenList.Add(MakeShared<FString>(Label));
		}
		ScreenList.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
		{ return A.IsValid() && B.IsValid() && *A < *B; });
	}

	// 一覧で選ばれた画面へ切り替える。goto: の解決基準（ScreenDir）も一緒に移す。
	void SelectScreen(const FString& RelLabel)
	{
		const FString Full = RootDir / (RelLabel + TEXT(".json"));
		ScreenDir = FPaths::GetPath(Full);
		CurrentScreen = FPaths::GetBaseFilename(Full);
		CurrentLabel = RelLabel;
		ScreenStack.Empty();
		Vars.clear();
		Load();
	}

	void Load()
	{
		bTreeLoaded = false;

		const FString Path = ScreenDir / CurrentScreen + TEXT(".json");
		FString ScreenJson;
		if (CurrentScreen.IsEmpty() || !FFileHelper::LoadFileToString(ScreenJson, *Path))
		{
			SetStatus(FString::Printf(TEXT("画面JSONを読めません: %s"), *Path));
			if (Canvas.IsValid()) Canvas->SetContent({}, {}, FLinearColor::Black);
			return;
		}
		PristineTree = KaedeCore::LoadScreenTree(TCHAR_TO_UTF8(*ScreenJson));
		bTreeLoaded = true;
		KaedeUI::SeedVarsFromTree(PristineTree, Vars);
		// 自動再読込の基準時刻。画面切替もここを通るので、切り替えた直後に
		// 「変わった」と誤検知しない。
		LastWriteTime = IFileManager::Get().GetTimeStamp(*Path);

		// 画面JSON の "title" をタブ名へ返す。生成器は同じ文字列を画面の見出しにも描くので、
		// タブと見出しが食い違わない（別々に持つと、同じ画面を2つの名前で呼ぶことになる）。
		if (OnTitleChanged)
		{
			FString Title;
			try
			{
				const auto J = nlohmann::json::parse(TCHAR_TO_UTF8(*ScreenJson));
				if (J.contains("title") && J["title"].is_string())
					Title = UTF8_TO_TCHAR(J["title"].get<std::string>().c_str());
			}
			catch (const std::exception&) { /* title が無い画面は素通り（既定のタブ名のまま） */ }
			OnTitleChanged(Title);
		}

		// 初期テーマ: theme.json があればそれ、無ければ組み込み atelier。
		FString ThemeJson;
		if (!ThemePath.IsEmpty() && FFileHelper::LoadFileToString(ThemeJson, *ThemePath))
		{
			CurrentTheme = KaedeCore::LoadTheme(TCHAR_TO_UTF8(*ThemeJson));
			CurrentThemeName = TEXT("file");
		}
		else if (CurrentThemeName != TEXT("file"))
		{
			CurrentTheme = KaedeCore::BuiltinTheme(TCHAR_TO_UTF8(*CurrentThemeName));
		}
		Rebuild();
	}

	void SetTheme(const FString& Name)
	{
		CurrentThemeName = Name;
		CurrentTheme = KaedeCore::BuiltinTheme(TCHAR_TO_UTF8(*Name));
		// 設定画面の「テーマ」ToggleGroup（bindVar uiTheme）にも反映して見た目と選択を同期。
		const int32 Idx = Name == TEXT("midnight") ? 1 : (Name == TEXT("sakura") ? 2 : 0);
		if (Vars.count("uiTheme")) Vars["uiTheme"] = (double)Idx;
		Rebuild();
	}

	void SetTouch(bool bInTouch)
	{
		bTouch = bInTouch;
		Rebuild();
	}

	// アクション文字列（プレビューの規約。goto:/close は設計書§11 の共通遷移テーブルに対応）:
	//   set:<var>=<num> … 変数代入（タブ/セグメント選択）
	//   slider:<var>    … relX(0..1) を変数へ（クリック/ドラッグ）
	//   toggle:<var>    … 反転
	//   goto:<screen>   … Game/ui/<screen>.json へ遷移（画面スタックに積む）
	//   close           … 遷移元へ戻る
	//   reset           … JSON 既定値へ戻す
	//   それ以外（newgame/continue/quit/jump:* 等）はステータス表示のみ（未結線）
	void HandleAction(const FString& Action, float RelX)
	{
		if (Action.StartsWith(TEXT("goto:")))
		{
			ScreenStack.Push(CurrentScreen);
			CurrentScreen = Action.Mid(5);
			Load();
			return;
		}
		if (Action == TEXT("close"))
		{
			if (ScreenStack.Num() > 0)
			{
				CurrentScreen = ScreenStack.Pop();
				Load();
			}
			else
			{
				SetStatus(TEXT("close: 戻り先なし（プレビュー起点の画面）"));
			}
			return;
		}
		// set:/slider:/toggle:/reset はゲームと同じ 1 本（KaedeUI::ApplyShellVarAction）。プレビューの reset は変数を空にしてから既定値へ
		if (Action == TEXT("reset")) Vars.clear();
		FString VarName;
		const KaedeUI::EKaedeVarAction VarKind = KaedeUI::ApplyShellVarAction(Action, RelX,
			bTreeLoaded ? PristineTree : kaedevn::ui::UiNode(), Vars, &VarName);
		if (VarKind == KaedeUI::EKaedeVarAction::Set && VarName == TEXT("uiTheme"))
		{
			// 設定画面内の「テーマ」選択はプレビューの実テーマも切り替える（実演）。
			const int32 Idx = (int32)Vars[TCHAR_TO_UTF8(*VarName)];
			CurrentThemeName = Idx == 1 ? TEXT("midnight") : (Idx == 2 ? TEXT("sakura") : TEXT("atelier"));
			CurrentTheme = KaedeCore::BuiltinTheme(TCHAR_TO_UTF8(*CurrentThemeName));
		}
		if (VarKind == KaedeUI::EKaedeVarAction::None && !Action.StartsWith(TEXT("set:")))
		{
			SetStatus(FString::Printf(TEXT("action: %s（プレビューでは未結線）"), *Action));
			return;
		}
		Rebuild();
	}

	// 公開の入口 PresentScreen で描く（変数の差し込み → 条件で刈る → 配置 → キャンバスへ。ゲームと同じ順）。
	// 変数はプレビュー内の Vars に、showIf/hideIf 用の端末の変数 touch を足して渡す。
	void Rebuild()
	{
		if (!bTreeLoaded || !Canvas.IsValid()) return;

		KaedeUI::FKaedeScreenValues Values;
		Values.Vars = Vars;
		Values.Vars["touch"] = bTouch ? 1.0 : 0.0;  // showIf/hideIf 用のプラットフォーム変数

		const FLinearColor BgColor(CurrentTheme.bg.r / 255.f, CurrentTheme.bg.g / 255.f,
		                           CurrentTheme.bg.b / 255.f, CurrentTheme.bg.a / 255.f);
		KaedeUI::PresentScreen(*Canvas, PristineTree, Values, CurrentTheme, BgColor, nullptr, TEXT("preview:") + CurrentScreen);
		SetStatus(FString::Printf(TEXT("%s.json — %d 描画コマンド / %d ヒット / テーマ %s / %s"),
			*CurrentScreen, Canvas->GetCmds().Num(), Canvas->GetHits().Num(),
			*CurrentThemeName, bTouch ? TEXT("タッチ") : TEXT("PC")));
	}

	void SetStatus(const FString& S) { if (StatusBlock.IsValid()) StatusBlock->SetText(FText::FromString(S)); }

	FString ScreenPath, ThemePath;
	FString RootDir;                // 画面一覧を集める置き場（呼ぶ側が渡す）
	FString ListFilter;             // 一覧の絞り込み（部分一致・空なら全部）
	bool bShowStyleControls = true; // テーマ／端末の切替を出すか
	TFunction<void(const FString&)> OnTitleChanged;  // 画面の "title" をタブ名へ返す
	FString ScreenDir;              // 画面JSONのフォルダ（goto: 遷移の解決基準）
	FString CurrentScreen;          // 現在の画面名（拡張子なし）
	FString CurrentLabel;           // 一覧に出している短い名前（例 "adv/ui/route-map"）
	TArray<TSharedPtr<FString>> ScreenList;  // Game/**/ui/*.json を走査した一覧
	TArray<FString> ScreenStack;    // goto: の戻り先スタック
	TSharedPtr<SKaedeUiCanvas> Canvas;
	TSharedPtr<STextBlock> StatusBlock;

	kaedevn::ui::UiNode PristineTree;             // 読み込んだままの画面ツリー（Rebuild の原本）
	bool bTreeLoaded = false;
	std::unordered_map<std::string, double> Vars; // bindVar の現在値（プレビュー内メモリ）
	kaedevn::ui::UiTheme CurrentTheme;
	FString CurrentThemeName = TEXT("atelier");
	bool bTouch = false;
};

TSharedRef<SWidget> MakeKaedeMomijiUIPreview(const FString& ScreenPath, const FString& ThemePath,
                                           const FString& RootDir,
                                           const FString& ListFilter, bool bShowStyleControls,
                                           TFunction<void(const FString&)> OnTitleChanged)
{
	return SNew(SKaedeMomijiUIPreview).ScreenPath(ScreenPath).ThemePath(ThemePath).RootDir(RootDir)
		.ListFilter(ListFilter).bShowStyleControls(bShowStyleControls)
		.OnTitleChanged(MoveTemp(OnTitleChanged));
}

#undef LOCTEXT_NAMESPACE
