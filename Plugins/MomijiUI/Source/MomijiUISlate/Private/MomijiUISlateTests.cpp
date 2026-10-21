// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#include "Misc/AutomationTest.h"
#include "KaedeUiPainter.h"
#include "MomijiUICore.h"
#include "SKaedeUiCanvas.h"   // MomijiUI.TextInputCanvas（編集ウィジェットの配置）
#include "ui/Tween.hpp"   // UiMotionState
#include "Layout/ArrangedChildren.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Examples/SMomijiUICounterExample.h"   // MomijiUI.ManualExample（説明書の見本）
#include "MomijiUIProjectPaths.h"                // プロジェクトの画面 JSON の置き場（[MomijiUI] ScreenRoot）・プラグインの置き場（見本・照合）
#include "Input/HittestGrid.h"       // MomijiUI.TextEditorClip（Slate の当たり判定が編集器をクリップで切るか）
#include "Widgets/SWindow.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include <nlohmann/json.hpp>

// MomijiUI の Slate の描画器（MomijiUISlate）の契約テスト MomijiUI.*（2026-10-14 に KaedeUITests.cpp から分けた）。
// 画面 JSON → ComputeDrawList の論理座標・当たり判定・焦点・入力を、作品の画面に依らずに見る。
// 作品の画面（Game/ui の title・menu 等）の到達性は KaedeVN 側の Kaede.UI.FocusReachability（KaedeVnUITests.cpp）。
// Core の純関数は MomijiUICore の MomijiUI.Core.*（MomijiUICoreTests.cpp・KaedeUiMotionTests.cpp）。

// ===== テストの補助（2026-10-18・Issue #76: 各テストに写していたものをここ 1 か所に）=====
namespace
{
	using namespace KaedeUI;

	// 固定幅の字の計測（1 字 PerChar px）。Slate の字に依らない配置のテストで使う
	FKaedeMeasureFn FixedMeasure(float PerChar = 11.f)
	{
		return [PerChar](const FString& S, bool, float) { return (float)S.Len() * PerChar; };
	}

	// action で当たりを探す（最初の一致。無ければ nullptr）
	const FKaedeHit* FindHit(const TArray<FKaedeHit>& Hits, const TCHAR* Action)
	{
		for (const FKaedeHit& H : Hits) if (H.Action == Action) return &H;
		return nullptr;
	}

	// 条件に合う最初の描画命令（無ければ nullptr）と、合う描画命令の数
	const FKaedeDrawCmd* FindCmd(const TArray<FKaedeDrawCmd>& Cmds, TFunctionRef<bool(const FKaedeDrawCmd&)> Pred)
	{
		for (const FKaedeDrawCmd& C : Cmds) if (Pred(C)) return &C;
		return nullptr;
	}
	int32 CountCmds(const TArray<FKaedeDrawCmd>& Cmds, TFunctionRef<bool(const FKaedeDrawCmd&)> Pred)
	{
		int32 N = 0;
		for (const FKaedeDrawCmd& C : Cmds) if (Pred(C)) ++N;
		return N;
	}

	// 照合（MomijiUI.MomijiUIConformance）が書き出す描画命令の要約。ブラウザ側は Tools/momiji-ui の primsOf
	nlohmann::json PrimsOf(const TArray<FKaedeDrawCmd>& Cmds)
	{
		nlohmann::json Out = nlohmann::json::array();
		for (const FKaedeDrawCmd& C : Cmds)
		{
			switch (C.Kind)
			{
			case EKaedeDrawKind::Rect:        Out.push_back({ {"kind", "Rect"}, {"x", C.X}, {"y", C.Y}, {"w", C.W}, {"h", C.H} }); break;
			case EKaedeDrawKind::RectOutline: Out.push_back({ {"kind", "RectOutline"}, {"x", C.X}, {"y", C.Y}, {"w", C.W}, {"h", C.H} }); break;
			case EKaedeDrawKind::Text:        Out.push_back({ {"kind", "Text"}, {"text", TCHAR_TO_UTF8(*C.Text)}, {"x", C.X}, {"y", C.Y} }); break;
			case EKaedeDrawKind::Image:       Out.push_back({ {"kind", "Image"}, {"x", C.X}, {"y", C.Y}, {"w", C.W}, {"h", C.H} }); break;
			case EKaedeDrawKind::Ellipse:     Out.push_back({ {"kind", "Ellipse"}, {"x", C.X}, {"y", C.Y}, {"w", C.W}, {"h", C.H} }); break;
			case EKaedeDrawKind::Lines:
			{
				float X0 = 1e9f, Y0 = 1e9f, X1 = -1e9f, Y1 = -1e9f;
				for (const FVector2f& P : C.Points) { X0 = FMath::Min(X0, P.X); Y0 = FMath::Min(Y0, P.Y); X1 = FMath::Max(X1, P.X); Y1 = FMath::Max(Y1, P.Y); }
				Out.push_back({ {"kind", "Lines"}, {"x", X0}, {"y", Y0}, {"w", X1 - X0}, {"h", Y1 - Y0}, {"n", C.Points.Num()} });
				break;
			}
			default: break;   // ClipPush/Pop・TextField（枠は Rect で出ている）は比べない
			}
		}
		return Out;
	}

	// 木の部品の数（根を含む）
	int32 CountNodes(const kaedevn::ui::UiNode& N)
	{
		int32 C = 1;
		for (const kaedevn::ui::UiNode& Ch : N.children) C += CountNodes(Ch);
		return C;
	}
}

// ScaleUiTree（Game/ui/*.json を論理キャンバス倍率へ合わせる純関数）の契約。
// 1280x720 基準の寸法が 1.5 倍になり、等倍は完全な no-op であること。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiScaleTreeTest, "MomijiUI.ScaleUiTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiScaleTreeTest::RunTest(const FString& Parameters)
{
	const char* kJson = R"JSON(
{ "type":"Panel","x":10,"y":20,"w":560,"h":64,"gap":8,"pad":16,"children":[
  { "type":"Label","text":"L","x":4,"y":6,"w":100,"h":30,"fontSize":16,"wrapW":200 },
  { "type":"Polyline","thickness":2,"points":[[40,60],[40,120],[200,120]] },
  { "type":"VerticalLayout","w":200,"h":100,"repeat":"items","template":
    { "type":"Button","h":50,"fontSize":18 } }
]}
)JSON";

	// 等倍は 1bit も変えない（720p 運用の回帰ゼロを保証する）。
	{
		kaedevn::ui::UiNode T = KaedeCore::LoadScreenTree(kJson);
		KaedeCore::ScaleUiTree(T, 1.0, 1.0);
		TestEqual(TEXT("identity keeps w"), T.w, 560);
		TestEqual(TEXT("identity keeps fontPt"), T.children[0].fontPt, 16);
		// 幾何も等倍で不変（ADV の図＝Polyline が 720p で 1bit も動かないことの保証）。
		TestEqual(TEXT("identity keeps point x"), T.children[1].points[1].x, 40);
		TestEqual(TEXT("identity keeps thickness"), T.children[1].thickness, 2.0f);
	}

	// 1.5 倍。fontPt=0（ホスト既定）は 0 のまま。repeatTemplate も追従する。
	{
		kaedevn::ui::UiNode T = KaedeCore::LoadScreenTree(kJson);
		const kaedevn::ui::UiNode Pristine = T;
		KaedeCore::ScaleUiTree(T, 1.5, 1.5);
		TestEqual(TEXT("panel x"), T.x, 15);
		TestEqual(TEXT("panel w"), T.w, 840);
		TestEqual(TEXT("panel h"), T.h, 96);
		TestEqual(TEXT("panel pad"), T.pad, 24);
		TestEqual(TEXT("label fontPt"), T.children[0].fontPt, 24);
		TestEqual(TEXT("label wrapW"), T.children[0].wrapW, 300);
		// Polyline の幾何は x/y と同じ空間。ここが動かないと札だけ動いて線が取り残される
		// （2026-08-29 に発見。ADV の事件の図が Polyline で出来ている）。
		TestEqual(TEXT("polyline point x"), T.children[1].points[2].x, 300);
		TestEqual(TEXT("polyline point y"), T.children[1].points[2].y, 180);
		TestEqual(TEXT("polyline thickness"), T.children[1].thickness, 3.0f);
		if (T.children[2].repeatTemplate)
			TestEqual(TEXT("repeat template h"), T.children[2].repeatTemplate->h, 75);
		else
			AddError(TEXT("repeatTemplate が失われた"));
		// pristine 側の repeatTemplate は共有 shared_ptr なので汚してはいけない。
		if (Pristine.children[2].repeatTemplate)
			TestEqual(TEXT("pristine template untouched"), Pristine.children[2].repeatTemplate->h, 50);
	}

	return true;
}

// showIf/enabledIf/highlightIf に合成条件（&& || ! 括弧）が効くことと、
// enabledIf/highlightIf が ApplyEnabledTree で実際に評価されることの契約。
// 2026-08-29 まで isVisible/applyEnabled は evalCond（比較1つ）を呼んでいて、
// 「狭幅で、かつ 詳細を開いている」のような2条件を1ノードで書けなかった
//（設計 CodexReports 2026/08/29/03 §10.4 が前提にしている）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiCompoundCondTest, "MomijiUI.CompoundCond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiCompoundCondTest::RunTest(const FString& Parameters)
{
	const char* kJson = R"JSON(
{ "type":"Panel","children":[
  { "type":"Label","id":"both","text":"A","showIf":"compact && detailOpen" },
  { "type":"Label","id":"either","text":"B","showIf":"compact || searchOpen" },
  { "type":"Label","id":"neg","text":"C","showIf":"!compact" },
  { "type":"Label","id":"paren","text":"D","showIf":"(compact || searchOpen) && detailOpen" },
  { "type":"Label","id":"plain","text":"E","showIf":"detailOpen" },
  { "type":"Button","id":"btn","text":"F","action":"x",
    "enabledIf":"compact && detailOpen","highlightIf":"compact && !searchOpen" }
]}
)JSON";

	auto IdsAfterPrune = [&](std::unordered_map<std::string, double> Vars)
	{
		kaedevn::ui::UiNode T = KaedeCore::LoadScreenTree(kJson);
		KaedeCore::PruneScreenTree(T, Vars);
		TSet<FString> Ids;
		for (const auto& C : T.children) Ids.Add(FString(UTF8_TO_TCHAR(C.id.c_str())));
		return Ids;
	};

	// compact=1, detailOpen=1, searchOpen=0
	{
		const TSet<FString> Ids = IdsAfterPrune({ {"compact",1.0},{"detailOpen",1.0},{"searchOpen",0.0} });
		TestTrue (TEXT("&& both true kept"),      Ids.Contains(TEXT("both")));
		TestTrue (TEXT("|| one true kept"),       Ids.Contains(TEXT("either")));
		TestFalse(TEXT("! on truthy pruned"),     Ids.Contains(TEXT("neg")));
		TestTrue (TEXT("parenthesised kept"),     Ids.Contains(TEXT("paren")));
		TestTrue (TEXT("plain var still works"),  Ids.Contains(TEXT("plain")));
	}
	// compact=1, detailOpen=0 → && は落ち、|| は残る
	{
		const TSet<FString> Ids = IdsAfterPrune({ {"compact",1.0},{"detailOpen",0.0},{"searchOpen",0.0} });
		TestFalse(TEXT("&& one false pruned"),    Ids.Contains(TEXT("both")));
		TestTrue (TEXT("|| still kept"),          Ids.Contains(TEXT("either")));
		TestFalse(TEXT("paren && false pruned"),  Ids.Contains(TEXT("paren")));
		TestFalse(TEXT("plain false pruned"),     Ids.Contains(TEXT("plain")));
	}
	// compact=0 → ! が真になる
	{
		const TSet<FString> Ids = IdsAfterPrune({ {"compact",0.0},{"detailOpen",1.0},{"searchOpen",0.0} });
		TestTrue (TEXT("! on falsy kept"),        Ids.Contains(TEXT("neg")));
		TestFalse(TEXT("&& needs compact"),       Ids.Contains(TEXT("both")));
	}

	// enabledIf / highlightIf は ApplyEnabledTree が評価する（prune は触らない）。
	{
		kaedevn::ui::UiNode T = KaedeCore::LoadScreenTree(kJson);
		KaedeCore::ApplyEnabledTree(T, { {"compact",1.0},{"detailOpen",1.0},{"searchOpen",0.0} });
		const kaedevn::ui::UiNode* Btn = nullptr;
		for (const auto& C : T.children) if (C.id == "btn") Btn = &C;
		if (!Btn) { AddError(TEXT("btn が見つからない")); return false; }
		TestTrue(TEXT("enabledIf compound true"),   Btn->enabled);
		TestTrue(TEXT("highlightIf compound true"), Btn->highlight);
	}
	{
		kaedevn::ui::UiNode T = KaedeCore::LoadScreenTree(kJson);
		KaedeCore::ApplyEnabledTree(T, { {"compact",1.0},{"detailOpen",0.0},{"searchOpen",1.0} });
		const kaedevn::ui::UiNode* Btn = nullptr;
		for (const auto& C : T.children) if (C.id == "btn") Btn = &C;
		if (!Btn) { AddError(TEXT("btn が見つからない")); return false; }
		TestFalse(TEXT("enabledIf compound false"),   Btn->enabled);
		TestFalse(TEXT("highlightIf negated false"),  Btn->highlight);
	}

	return true;
}

// Component Standard 画像拡張（Phase 3）: Image ノードが sprite/Fit 付きの Image コマンドを出し、
// icon-tile の器＋内側インセット、画像なし時のプレースホルダ/fallback を検証する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiImageTest, "MomijiUI.Image",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiImageTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"children":[
  {"type":"Image","variant":"atelier-icon-tile","x":10,"y":10,"w":80,"h":80,"sprite":"ui/shared/icons/herb.png","fit":"contain"},
  {"type":"Image","variant":"atelier-icon-tile","x":100,"y":10,"w":80,"h":80,"text":"薬"},
  {"type":"Image","x":200,"y":10,"w":120,"h":68,"sprite":"ui/shared/thumbs/slot1.png","fit":"cover"}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure);

	auto FindImage = [&Cmds](const TCHAR* Sprite) -> const FKaedeDrawCmd*
	{
		for (const FKaedeDrawCmd& C : Cmds)
			if (C.Kind == EKaedeDrawKind::Image && C.Sprite == Sprite) return &C;
		return nullptr;
	};

	// タイル1: sprite あり → 器（6px インセット）に収まる Image コマンド、fit=contain。
	const FKaedeDrawCmd* Herb = FindImage(TEXT("ui/shared/icons/herb.png"));
	TestTrue(TEXT("herb image emitted"), Herb != nullptr);
	if (Herb)
	{
		TestEqual(TEXT("herb inset x"), Herb->X, 16.f);   // 10 + pad6
		TestEqual(TEXT("herb inset y"), Herb->Y, 16.f);
		TestEqual(TEXT("herb inset w"), Herb->W, 68.f);   // 80 - 12
		TestEqual(TEXT("herb fit"), Herb->Fit, FString(TEXT("contain")));
	}
	// タイル1 の器（Tile ロール）が直角でなく角丸6（atelier）で描かれている。
	bool bTileFrame = false;
	for (const FKaedeDrawCmd& C : Cmds)
		if (C.Kind == EKaedeDrawKind::Rect && C.X == 10 && C.Y == 10 && C.W == 80 && C.H == 80 && C.Radius == 6.f)
			bTileFrame = true;
	TestTrue(TEXT("tile frame rounded"), bTileFrame);

	// タイル2: sprite なし → Image コマンドは出ず、fallback 文字「薬」がテキストで出る。
	TestTrue(TEXT("no image without sprite"), FindImage(TEXT("")) == nullptr);
	bool bFallback = false;
	for (const FKaedeDrawCmd& C : Cmds)
		if (C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("薬")) bFallback = true;
	TestTrue(TEXT("fallback char emitted"), bFallback);

	// スロット: fit=cover が保持される。
	const FKaedeDrawCmd* Slot = FindImage(TEXT("ui/shared/thumbs/slot1.png"));
	TestTrue(TEXT("slot image emitted"), Slot != nullptr);
	if (Slot) TestEqual(TEXT("slot fit cover"), Slot->Fit, FString(TEXT("cover")));

	return true;
}

// Focus 土台（Phase 2）: hit に focusable/FocusGroup/FocusId が乗り、Modal 配下は別グループ・
// backdrop は非フォーカスになることを検証する。
// 2026-10-03（仕様 §7・R1）: backdrop も Modal の群（+1）に入れた——操作対象の無い Modal でも最前面の群が上がり、
// 背景へ Enter が漏れない。backdrop は非フォーカスのまま。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiFocusHitTest, "MomijiUI.FocusHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiFocusHitTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	// 背景ボタン（グループ0・focusable）＋ Modal（backdrop 非focusable・子は別グループ）。
	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"children":[
  {"type":"Button","id":"bg","text":"BG","x":10,"y":10,"w":120,"h":50,"action":"bg"},
  {"type":"Modal","w":400,"h":200,"dim":true,"pad":16,"gap":10,"children":[
    {"type":"Button","id":"ok","text":"OK","h":50,"action":"ok"},
    {"type":"Button","id":"ng","text":"NG","h":50,"focusable":false,"action":"ng"}
  ]}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	ComputeDrawList(Tree, 1280, 720, Measure, &Hits);


	const FKaedeHit* Bg = FindHit(Hits, TEXT("bg"));
	TestTrue(TEXT("bg hit exists"), Bg != nullptr);
	if (Bg)
	{
		TestTrue(TEXT("bg focusable"), Bg->bFocusable);
		TestEqual(TEXT("bg group 0"), Bg->FocusGroup, 0);
		TestEqual(TEXT("bg id"), Bg->FocusId, FString(TEXT("bg")));
	}
	// Modal backdrop: action 空・非フォーカス・Modal の群（+1）。
	const FKaedeHit* Backdrop = FindHit(Hits, TEXT(""));
	TestTrue(TEXT("backdrop hit exists"), Backdrop != nullptr);
	if (Backdrop)
	{
		TestFalse(TEXT("backdrop not focusable"), Backdrop->bFocusable);
		TestEqual(TEXT("backdrop group 1 (Modal の群)"), Backdrop->FocusGroup, 1);
	}
	// Modal 内 OK: 別グループ（+1）・focusable。
	const FKaedeHit* Ok = FindHit(Hits, TEXT("ok"));
	TestTrue(TEXT("ok hit exists"), Ok != nullptr);
	if (Ok)
	{
		TestTrue(TEXT("ok focusable"), Ok->bFocusable);
		TestEqual(TEXT("ok group 1"), Ok->FocusGroup, 1);
	}
	// focusable:false の NG は hit は出るが bFocusable=false。
	const FKaedeHit* Ng = FindHit(Hits, TEXT("ng"));
	TestTrue(TEXT("ng hit exists"), Ng != nullptr);
	if (Ng) TestFalse(TEXT("ng not focusable"), Ng->bFocusable);

	return true;
}

// enabledIf 束縛: repeat の "{item.can}" とグローバル数値変数の両経路で、偽なら
// enabled=false（disabled 描画＋ヒット不成立）になるか。action は実体を保ったまま
// 押せなくなるのが契約（空 action フォールバックの禁止）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiEnabledIfTest, "MomijiUI.EnabledIf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiEnabledIfTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"children":[
  {"type":"VerticalLayout","x":0,"y":0,"w":600,"h":400,"repeat":"rows","template":
    {"type":"Button","text":"{item.label}","h":50,"action":"do:{item.id}","enabledIf":"{item.can}"}},
  {"type":"Button","id":"g","text":"G","x":700,"y":10,"w":120,"h":50,"action":"global","enabledIf":"gate"}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	std::unordered_map<std::string, std::string> Str;
	std::unordered_map<std::string, double> Num;
	Num["gate"] = 0.0;   // グローバル変数経路: 偽
	std::unordered_map<std::string, std::vector<kaedevn::ui::ItemScope>> Lists;
	Lists["rows"] = {
		{ {"id", "a"}, {"label", "A"}, {"can", "1"} },
		{ {"id", "b"}, {"label", "B"}, {"can", "0"} },
	};
	KaedeCore::BindScreenTree(Tree, Str, Num, Lists);

	TArray<FKaedeHit> Hits;
	ComputeDrawList(Tree, 1280, 720, Measure, &Hits);

	// can=1 の行はヒットあり。can=0 の行は action が実体でもヒット不成立。
	TestTrue(TEXT("row a (can=1) has hit"), FindHit(Hits, TEXT("do:a")) != nullptr);
	TestTrue(TEXT("row b (can=0) has no hit"), FindHit(Hits, TEXT("do:b")) == nullptr);
	// グローバル変数 gate=0 → 押せない。
	TestTrue(TEXT("global gate=0 has no hit"), FindHit(Hits, TEXT("global")) == nullptr);

	// enabled フラグ自体も落ちている（painter が disabled 色で描く前提の直接確認）。
	const kaedevn::ui::UiNode* RowB = nullptr;
	for (const auto& C : Tree.children[0].children)
		if (C.action == "do:b") RowB = &C;
	TestTrue(TEXT("row b node found"), RowB != nullptr);
	if (RowB) TestFalse(TEXT("row b enabled=false"), RowB->enabled);

	return true;
}

// Focus Graph（Phase 3）: BuildFocusNav が縦リスト/横タブ/グリッド/グループ分離を正しく張るか。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiFocusNavTest, "MomijiUI.FocusNav",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiFocusNavTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;

	// 2x2 グリッド（group0）＋ 別グループ(1)の1個。index: 0=TL,1=TR,2=BL,3=BR,4=other-group。
	TArray<FKaedeHit> Hits;
	auto Mk = [](float x, float y, int32 grp) { FKaedeHit H; H.X=x; H.Y=y; H.W=100; H.H=50; H.FocusGroup=grp; H.bFocusable=true; return H; };
	Hits.Add(Mk(0,   0,   0));   // 0 TL
	Hits.Add(Mk(200, 0,   0));   // 1 TR
	Hits.Add(Mk(0,   200, 0));   // 2 BL
	Hits.Add(Mk(200, 200, 0));   // 3 BR
	Hits.Add(Mk(500, 500, 1));   // 4 別グループ（到達不可のはず）

	const TArray<FKaedeFocusNav> Nav = BuildFocusNav(Hits);

	// TL: 右=TR, 下=BL, 上/左=なし。
	TestEqual(TEXT("TL.Right=TR"), Nav[0].Right, 1);
	TestEqual(TEXT("TL.Down=BL"),  Nav[0].Down, 2);
	TestEqual(TEXT("TL.Up none"),  Nav[0].Up, -1);
	TestEqual(TEXT("TL.Left none"),Nav[0].Left, -1);
	// BR: 左=BL, 上=TR。
	TestEqual(TEXT("BR.Left=BL"),  Nav[3].Left, 2);
	TestEqual(TEXT("BR.Up=TR"),    Nav[3].Up, 1);
	// グループ跨ぎは張らない（0番から4番へは行けない）。
	TestTrue(TEXT("no cross-group from TL"),
		Nav[0].Up != 4 && Nav[0].Down != 4 && Nav[0].Left != 4 && Nav[0].Right != 4);
	// 別グループ(4)は同グループに相手がいないので全方向なし。
	TestTrue(TEXT("other group isolated"),
		Nav[4].Up == -1 && Nav[4].Down == -1 && Nav[4].Left == -1 && Nav[4].Right == -1);

	return true;
}

// Focus 到達性（Phase 9 / doc13 完了条件「Focus できないボタンを検出」）:
// 最上位 FocusGroup の先頭 focusable を初期フォーカスに、Up/Down/Left/Right を BFS して
//  1) 到達集合が同一グループ内に閉じる（Modal 外へ出ない）
//  2) 最上位グループの action 付き focusable が全部到達可能（＝Focus できないボタンが無い）
// を、合成ケース（グリッド・縦リスト・Modal）で検証する。
// 実画面 JSON（VN の title/menu/save/load/toc/config）の到達性は KaedeVN 側の Kaede.UI.FocusReachability
//（2026-10-18・Issue #76: プラグインのテストがプロジェクトの画面と ini に依存していたのを分けた）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiFocusReachabilityTest, "MomijiUI.FocusReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiFocusReachabilityTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;

	auto TopGroup = [](const TArray<FKaedeHit>& H) -> int32
	{
		int32 G = 0; for (const FKaedeHit& h : H) G = FMath::Max(G, h.FocusGroup); return G;
	};
	auto FirstFocusableInGroup = [](const TArray<FKaedeHit>& H, int32 Grp) -> int32
	{
		for (int32 i = 0; i < H.Num(); ++i) if (H[i].bFocusable && H[i].FocusGroup == Grp) return i;
		return -1;
	};
	auto Reachable = [](const TArray<FKaedeHit>& H, const TArray<FKaedeFocusNav>& N, int32 Start) -> TSet<int32>
	{
		TSet<int32> Seen;
		if (!H.IsValidIndex(Start) || !N.IsValidIndex(Start)) return Seen;
		TArray<int32> Stack; Stack.Push(Start); Seen.Add(Start);
		while (Stack.Num() > 0)
		{
			const int32 i = Stack.Pop();
			const FKaedeFocusNav& Nv = N[i];
			for (int32 Next : { Nv.Up, Nv.Down, Nv.Left, Nv.Right })
				if (H.IsValidIndex(Next) && !Seen.Contains(Next)) { Seen.Add(Next); Stack.Push(Next); }
		}
		return Seen;
	};

	// 共通チェック本体。到達数（合成ケースの厳密確認用）を返す。
	auto Check = [&](const TArray<FKaedeHit>& H, const TCHAR* Label) -> int32
	{
		const int32 Grp = TopGroup(H);
		const int32 Start = FirstFocusableInGroup(H, Grp);
		TestTrue(*FString::Printf(TEXT("%s: has initial focus"), Label), Start >= 0);
		if (Start < 0) return 0;
		const TArray<FKaedeFocusNav> Nav = BuildFocusNav(H);
		const TSet<int32> Seen = Reachable(H, Nav, Start);
		// 1) 到達先はすべて最上位グループ内（Modal 外へ漏れない）。
		bool bStaysInGroup = true;
		for (int32 i : Seen) if (H[i].FocusGroup != Grp) bStaysInGroup = false;
		TestTrue(*FString::Printf(TEXT("%s: reachable set stays in top group"), Label), bStaysInGroup);
		// 2) 最上位グループの action 付き focusable は全部到達可能。未到達は Focus できないボタン。
		for (int32 i = 0; i < H.Num(); ++i)
			if (H[i].bFocusable && H[i].FocusGroup == Grp && !H[i].Action.IsEmpty() && !Seen.Contains(i))
				AddError(FString::Printf(TEXT("%s: focusable button unreachable: '%s'"),
					Label, *H[i].Action));
		return Seen.Num();
	};

	const FKaedeMeasureFn Measure = FixedMeasure();
	auto Mk = [](float x, float y, int32 grp, const TCHAR* act)
	{
		FKaedeHit H; H.X = x; H.Y = y; H.W = 100; H.H = 50; H.FocusGroup = grp; H.bFocusable = true;
		H.Action = act; return H;
	};

	// 合成1: 2x2 グリッド（全4個が相互到達）。
	{
		TArray<FKaedeHit> H = { Mk(0,0,0,TEXT("a")), Mk(200,0,0,TEXT("b")),
		                        Mk(0,200,0,TEXT("c")), Mk(200,200,0,TEXT("d")) };
		TestEqual(TEXT("grid: all 4 reachable"), Check(H, TEXT("grid")), 4);
	}
	// 合成2: 縦リスト5個（save/load スロット相当）。
	{
		TArray<FKaedeHit> H;
		for (int32 i = 0; i < 5; ++i) H.Add(Mk(100, (float)(i * 80), 0, *FString::Printf(TEXT("slot%d"), i)));
		TestEqual(TEXT("list: all 5 reachable"), Check(H, TEXT("list")), 5);
	}
	// 合成3: 実 Modal（backdrop 非focusable・子は別グループ）。到達は子グループ内で完結し
	// backdrop（group0）へ出ない。
	{
		const char* kJson = R"JSON(
{ "type":"Modal","w":600,"h":400,"dim":true,"pad":16,"gap":10,"children":[
  { "type":"HorizontalLayout","h":50,"children":[
    {"type":"Button","text":"A","action":"m:a"},
    {"type":"Button","text":"B","action":"m:b"} ] },
  {"type":"Button","text":"Close","h":50,"action":"close"}
]}
)JSON";
		kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
		TArray<FKaedeHit> H;
		ComputeDrawList(Tree, 1280, 720, Measure, &H);
		Check(H, TEXT("modal"));
		// backdrop（action 空・group0）が到達集合に混ざらないことを念のため確認。
		const int32 Grp = TopGroup(H);
		TestTrue(TEXT("modal: top group is child group (>0)"), Grp > 0);
	}
	return true;
}

// ScrollView（Phase 6）: 縦積みレイアウト＋ClipPush/ClipPop＋hit の ScrollId 付与・contentH 計算。
// 実際のクリップ/スクロールは Canvas 側（本テストは純関数の幾何のみ）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiScrollViewTest, "MomijiUI.ScrollView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiScrollViewTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	// id=sv1・viewport(40,60,400,200)・pad10/gap8・50px 行×5 → contentH=302（>200 でスクロール）。
	const char* kJson = R"JSON(
{ "type":"ScrollView","id":"sv1","x":40,"y":60,"w":400,"h":200,"pad":10,"gap":8,"children":[
  {"type":"Button","text":"R0","h":50,"action":"r0"},
  {"type":"Button","text":"R1","h":50,"action":"r1"},
  {"type":"Button","text":"R2","h":50,"action":"r2"},
  {"type":"Button","text":"R3","h":50,"action":"r3"},
  {"type":"Button","text":"R4","h":50,"action":"r4"}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	const TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure, &Hits);

	// ClipPush: ビューポート矩形・id・contentH。
	const FKaedeDrawCmd* Push = nullptr;
	int32 PushIdx = -1, PopIdx = -1;
	for (int32 i = 0; i < Cmds.Num(); ++i)
	{
		if (Cmds[i].Kind == EKaedeDrawKind::ClipPush) { Push = &Cmds[i]; PushIdx = i; }
		if (Cmds[i].Kind == EKaedeDrawKind::ClipPop)  PopIdx = i;
	}
	TestTrue(TEXT("ClipPush emitted"), Push != nullptr);
	if (Push)
	{
		TestEqual(TEXT("clip viewport x"), Push->X, 40.f);
		TestEqual(TEXT("clip viewport w"), Push->W, 400.f);
		TestEqual(TEXT("clip viewport h"), Push->H, 200.f);
		TestEqual(TEXT("clip scroll id"), Push->Sprite, FString(TEXT("sv1")));
		TestEqual(TEXT("clip contentH"), Push->ContentH, 302.f);
	}
	TestTrue(TEXT("ClipPop emitted after ClipPush"), PopIdx > PushIdx);

	// 全行の hit が emit され、ScrollId=sv1 が付く（可視カリングは Canvas 側なので全件出る）。
	for (const TCHAR* A : { TEXT("r0"), TEXT("r1"), TEXT("r2"), TEXT("r3"), TEXT("r4") })
	{
		const FKaedeHit* H = FindHit(Hits, A);
		TestTrue(*FString::Printf(TEXT("hit %s exists"), A), H != nullptr);
		if (H) TestEqual(*FString::Printf(TEXT("hit %s scrollId"), A), H->ScrollId, FString(TEXT("sv1")));
	}
	// フルコンテンツ座標（クリップ前）: 行0 y=70, 行1 y=128（=70+50+8）。
	auto HitY = [&Hits](const TCHAR* A) -> float { const FKaedeHit* H = FindHit(Hits, A); return H ? H->Y : -1.f; };
	TestEqual(TEXT("row0 y (full content)"), HitY(TEXT("r0")), 70.f);
	TestEqual(TEXT("row1 y (full content)"), HitY(TEXT("r1")), 128.f);
	TestEqual(TEXT("row4 y (below viewport, full content)"), HitY(TEXT("r4")), 302.f);

	return true;
}

// GridLayout（Phase 6）: cols 列の等分セル配置と、幾何ベースの方向フォーカス（グリッド遷移）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiGridLayoutTest, "MomijiUI.GridLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiGridLayoutTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	// cols=2・w420/pad10/gap10 → cellW=(420-20-10)/2=195。80px セル×4（2x2）。
	const char* kJson = R"JSON(
{ "type":"GridLayout","x":0,"y":0,"w":420,"h":400,"cols":2,"pad":10,"gap":10,"children":[
  {"type":"Button","text":"C0","h":80,"action":"c0"},
  {"type":"Button","text":"C1","h":80,"action":"c1"},
  {"type":"Button","text":"C2","h":80,"action":"c2"},
  {"type":"Button","text":"C3","h":80,"action":"c3"}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	ComputeDrawList(Tree, 1280, 720, Measure, &Hits);

	// セル位置: C0(10,10,195) C1(215,10) C2(10,100) C3(215,100)。
	const FKaedeHit* C0 = FindHit(Hits, TEXT("c0")); const FKaedeHit* C1 = FindHit(Hits, TEXT("c1"));
	const FKaedeHit* C2 = FindHit(Hits, TEXT("c2")); const FKaedeHit* C3 = FindHit(Hits, TEXT("c3"));
	TestTrue(TEXT("4 cells exist"), C0 && C1 && C2 && C3);
	if (C0 && C1 && C2 && C3)
	{
		TestEqual(TEXT("C0 x"), C0->X, 10.f);  TestEqual(TEXT("C0 y"), C0->Y, 10.f);
		TestEqual(TEXT("C0 w=cellW"), C0->W, 195.f);
		TestEqual(TEXT("C1 x=col2"), C1->X, 215.f); TestEqual(TEXT("C1 y"), C1->Y, 10.f);
		TestEqual(TEXT("C2 y=row2"), C2->Y, 100.f); TestEqual(TEXT("C2 x"), C2->X, 10.f);
		TestEqual(TEXT("C3 x"), C3->X, 215.f);       TestEqual(TEXT("C3 y"), C3->Y, 100.f);
	}

	// 方向フォーカスがグリッド遷移になる（純関数）。
	const TArray<FKaedeFocusNav> Nav = BuildFocusNav(Hits);
	const int32 i0 = Hits.IndexOfByPredicate([](const FKaedeHit& H){ return H.Action == TEXT("c0"); });
	const int32 i1 = Hits.IndexOfByPredicate([](const FKaedeHit& H){ return H.Action == TEXT("c1"); });
	const int32 i2 = Hits.IndexOfByPredicate([](const FKaedeHit& H){ return H.Action == TEXT("c2"); });
	const int32 i3 = Hits.IndexOfByPredicate([](const FKaedeHit& H){ return H.Action == TEXT("c3"); });
	TestEqual(TEXT("C0.Right=C1"), Nav[i0].Right, i1);
	TestEqual(TEXT("C0.Down=C2"),  Nav[i0].Down, i2);
	TestEqual(TEXT("C3.Up=C1"),    Nav[i3].Up, i1);
	TestEqual(TEXT("C3.Left=C2"),  Nav[i3].Left, i2);

	return true;
}

// TextInput（Phase 6・描画）: 枠＋現在テキスト/placeholder＋focus 時 caret、クリック領域を検証。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiTextInputTest, "MomijiUI.TextInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiTextInputTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	auto HasRectAt = [](const TArray<FKaedeDrawCmd>& C, float X, float Y, float W, float H) -> bool
	{
		for (const FKaedeDrawCmd& c : C) if (c.Kind == EKaedeDrawKind::Rect && c.X == X && c.Y == Y && c.W == W && c.H == H) return true;
		return false;
	};

	// 枠 Rect＋TextField コマンド（id/placeholder/テキスト領域）＋クリック領域。本文/caret/編集は Canvas 側。
	const char* kJson = R"JSON(
{ "type":"TextInput","x":100,"y":80,"w":300,"h":48,"placeholder":"なまえ","action":"name" }
)JSON";
	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	const TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure, &Hits);

	TestTrue(TEXT("field bg rect"), HasRectAt(Cmds, 100.f, 80.f, 300.f, 48.f));
	const FKaedeDrawCmd* TF = nullptr;
	for (const FKaedeDrawCmd& c : Cmds) if (c.Kind == EKaedeDrawKind::TextField) TF = &c;
	TestTrue(TEXT("TextField cmd emitted"), TF != nullptr);
	if (TF)
	{
		TestEqual(TEXT("field id (=action fallback)"), TF->Sprite, FString(TEXT("name")));
		TestEqual(TEXT("placeholder carried"), TF->Text, FString(TEXT("なまえ")));
		TestEqual(TEXT("text area x (pad12)"), TF->X, 112.f);
		TestEqual(TEXT("text area w"), TF->W, 276.f);
		TestEqual(TEXT("fontSize 未指定は 0（キャンバス既定）"), TF->FontPt, 0.f);
	}
	// fontSize は欄の文字にも効く（2026-09-12: 手帳の検索欄 fontSize 13 が既定 16 で描かれていた）。
	{
		kaedevn::ui::UiNode T2 = KaedeCore::LoadScreenTree(R"JSON(
{ "type":"TextInput","x":100,"y":80,"w":300,"h":32,"placeholder":"探す","action":"find","fontSize":13 }
)JSON");
		TArray<FKaedeDrawCmd> C2 = ComputeDrawList(T2, 1280, 720, Measure);
		const FKaedeDrawCmd* TF2 = nullptr;
		for (const FKaedeDrawCmd& c : C2) if (c.Kind == EKaedeDrawKind::TextField) TF2 = &c;
		TestTrue(TEXT("TextField cmd emitted (fontSize)"), TF2 != nullptr);
		if (TF2) TestEqual(TEXT("fontSize carried to TextField"), TF2->FontPt, 13.f);
	}
	bool bHit = false; for (const FKaedeHit& H : Hits) if (H.Action == TEXT("name")) bHit = true;
	TestTrue(TEXT("field is clickable/focusable"), bHit);

	return true;
}

// TextInput（Canvas 側）: 編集は本物の SEditableText が担う（IME のため。2026-08-10）。
// ここで守りたいのは「編集中だけ子が現れ、描画と同じレターボックス位置に置かれる」こと。
// **この2つが崩れると、日本語は打てるのに枠の外に文字が出る**という壊れ方をする。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiTextInputCanvasTest, "MomijiUI.TextInputCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiTextInputCanvasTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	const char* kJson = R"JSON(
{ "type":"TextInput","x":100,"y":80,"w":300,"h":48,"placeholder":"なまえ","action":"name" }
)JSON";
	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure, &Hits);

	const TSharedRef<SKaedeUiCanvas> Canvas = SNew(SKaedeUiCanvas).LogicalW(1280).LogicalH(720);
	Canvas->SetContent(Cmds, Hits, FLinearColor::Black);

	// 論理1280x720 をちょうど2倍で表示する枠。レターボックスの余白は出ない。
	const FGeometry Geo = FGeometry::MakeRoot(FVector2f(2560.f, 1440.f), FSlateLayoutTransform());

	// 編集していない間は子が出てこない（当たり判定もレイアウトも汚さない）。
	{
		FArrangedChildren Arranged(EVisibility::Visible);
		Canvas->ArrangeChildren(Geo, Arranged);
		TestEqual(TEXT("idle: no editor child"), Arranged.Num(), 0);
	}

	// 値の往復。SetContent を跨いでも id 別バッファに残るのが従来からの契約。
	Canvas->SetTextInputValue(TEXT("name"), TEXT("霧走"));
	TestEqual(TEXT("buffer round trip"), Canvas->GetTextInputValue(TEXT("name")), FString(TEXT("霧走")));

	// 編集中は子が現れ、TextField の論理矩形（x=112 pad12 / w=276）に倍率2で載る。
	Canvas->SetActiveTextInput(TEXT("name"));
	TestEqual(TEXT("active field"), Canvas->ActiveTextInput(), FString(TEXT("name")));
	{
		FArrangedChildren Arranged(EVisibility::Visible);
		Canvas->ArrangeChildren(Geo, Arranged);
		TestEqual(TEXT("editing: editor child arranged"), Arranged.Num(), 1);
		if (Arranged.Num() == 1)
		{
			const FGeometry& G = Arranged[0].Geometry;
			const FVector2f Pos = FVector2f(G.GetAbsolutePosition());
			TestEqual(TEXT("editor x = 112*2"), Pos.X, 224.f);
			TestEqual(TEXT("editor w = 276*2"), (float)G.GetLocalSize().X, 552.f);

			// 縦は「枠いっぱい」ではなく1行ぶん。枠を渡すと SEditableText が上端から描いて
			// 文字が上へずれる（2026-08-10 の実機不具合）。高さはフォント依存なので、
			// **中心が非編集時の描画と一致すること**を見る＝クリックしても文字が動かない。
			const float BoxH = 48.f * 2.f;
			const float H = (float)G.GetLocalSize().Y;
			TestTrue(TEXT("editor h is one line (< box)"), H > 0.f && H < BoxH);
			// 上端は非編集時の描画（OnPaint の TextField）と同じ式＝基準の全角 1 字「あ」の字面を枠 (y=80, h=48)×倍率2 の中心へ置いた位置。
			// 2026-10-02（c3b2bfaf）に固定の字面補正 0.25em から字面での中央寄せへ変わった。旧い期待値（中心 200）はそのとき直し漏れていた。
			const FSlateFontInfo FieldFont = KaedeUI::MakeKaedeUiFont(false, 16 * 2);
			const float PainterTop = KaedeUI::KaedeInkCenteredTextPos(TEXT("あ"), FieldFont, FVector2f(0.f, 80.f * 2.f), 48.f * 2.f, false).Y;
			TestEqual(TEXT("editor top matches painter (ink-centered)"), Pos.Y, PainterTop, 0.51f);
		}
	}

	// 編集を抜けたら再び畳まれる。
	Canvas->SetActiveTextInput(FString());
	{
		FArrangedChildren Arranged(EVisibility::Visible);
		Canvas->ArrangeChildren(Geo, Arranged);
		TestEqual(TEXT("left editing: collapsed again"), Arranged.Num(), 0);
	}
	return true;
}

// Label runs（F1 リッチテキスト）: 区間ごとの色/太字＋ルビ furigana を Text 列に展開する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiRichTextTest, "MomijiUI.RichText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiRichTextTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	// ルビ付き太字 run ＋ 赤色 run。ルビ run は1片保持・非ルビ run は1コードポイントに割れる。
	const char* kJson = R"JSON(
{ "type":"Label","x":100,"y":200,"wrapW":600,"runs":[
  {"text":"漢字","ruby":"かんじ","bold":true},
  {"text":"だ","color":"#ff0000"}
]}
)JSON";
	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	const TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure);

	auto FindText = [&Cmds](const TCHAR* T) -> const FKaedeDrawCmd*
	{
		for (const FKaedeDrawCmd& C : Cmds) if (C.Kind == EKaedeDrawKind::Text && C.Text == T) return &C;
		return nullptr;
	};

	// ルビ run "漢字" は分割されず1片・太字・本文サイズ（FontPt=0）。
	const FKaedeDrawCmd* Base = FindText(TEXT("漢字"));
	TestTrue(TEXT("ruby-run base emitted whole"), Base != nullptr);
	if (Base) { TestTrue(TEXT("base is bold"), Base->bBold); TestEqual(TEXT("base default font"), Base->FontPt, 0.f); }

	// ルビ "かんじ" は小サイズ（FontPt=11）で base の上（y が base より小さい）。
	const FKaedeDrawCmd* Ruby = FindText(TEXT("かんじ"));
	TestTrue(TEXT("ruby emitted"), Ruby != nullptr);
	if (Ruby && Base)
	{
		TestEqual(TEXT("ruby small font"), Ruby->FontPt, 11.f);
		TestTrue(TEXT("ruby above base"), Ruby->Y < Base->Y);
	}

	// 非ルビ run "だ" は赤色で出る（1コードポイント）。
	const FKaedeDrawCmd* Red = FindText(TEXT("だ"));
	TestTrue(TEXT("colored run emitted"), Red != nullptr);
	if (Red) TestEqual(TEXT("run color red"), Red->Color, FColor(255, 0, 0, 255));

	return true;
}

// 図形（F-2）: Polyline / Ellipse が JSON から描画コマンドへ落ちること。
// 攻略地図のような「位置と線が意味を持つ図」を JSON だけで出すための最小契約。
// 生成物（Game/adv/ui/route-map*.json）には依存させない——あれは gitignore 済みで、
// 鮮度の罠を検査へ持ち込まないため。ここでは契約だけを直書きで固定する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiShapeTest, "MomijiUI.Shape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiShapeTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();

	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"children":[
  {"type":"Polyline","color":"#6fb4dd","thickness":2.5,"points":[[10,20],[30,40],[50,20]]},
  {"type":"Polyline","color":"#f0897a","thickness":2,"closed":true,"points":[[100,100],[200,100],[200,160],[100,160]]},
  {"type":"Polyline","color":"#ffffff","points":[[5,5]]},
  {"type":"Ellipse","x":300,"y":40,"w":12,"h":12,"color":"#52c39a","fill":true},
  {"type":"Ellipse","x":330,"y":40,"w":12,"h":12,"color":"#ddb05e","fill":false,"thickness":2}
]}
)JSON";

	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure);

	TArray<const FKaedeDrawCmd*> Lines, Ovals;
	for (const FKaedeDrawCmd& C : Cmds)
	{
		if (C.Kind == EKaedeDrawKind::Lines)   Lines.Add(&C);
		if (C.Kind == EKaedeDrawKind::Ellipse) Ovals.Add(&C);
	}

	// 頂点が2点未満の Polyline は描かない（線にならないため）。3本書いて出るのは2本。
	TestEqual(TEXT("Lines は2本（1点のものは落ちる）"), Lines.Num(), 2);
	TestEqual(TEXT("Ellipse は2つ"), Ovals.Num(), 2);
	if (Lines.Num() < 2 || Ovals.Num() < 2) return false;

	// 開いた折れ線: 頂点そのまま・線幅・色。
	TestEqual(TEXT("開いた線の頂点数"), Lines[0]->Points.Num(), 3);
	TestEqual(TEXT("頂点[0].X"), Lines[0]->Points[0].X, 10.f);
	TestEqual(TEXT("頂点[2].Y"), Lines[0]->Points[2].Y, 20.f);
	TestEqual(TEXT("線幅"), Lines[0]->Thickness, 2.5f);
	TestEqual(TEXT("線色"), Lines[0]->Color, FColor(0x6f, 0xb4, 0xdd, 0xff));

	// closed=true は終点に始点を足す＝枠線を1ノードで描ける（4頂点 → 5点）。
	TestEqual(TEXT("閉じた線は始点が末尾に足される"), Lines[1]->Points.Num(), 5);
	TestEqual(TEXT("閉じた線の末尾=始点X"), Lines[1]->Points[4].X, Lines[1]->Points[0].X);
	TestEqual(TEXT("閉じた線の末尾=始点Y"), Lines[1]->Points[4].Y, Lines[1]->Points[0].Y);

	// 楕円は外接矩形と塗りの有無。
	TestEqual(TEXT("楕円X"), Ovals[0]->X, 300.f);
	TestEqual(TEXT("楕円W"), Ovals[0]->W, 12.f);
	TestTrue(TEXT("fill 既定は塗り"), Ovals[0]->bFill);
	TestFalse(TEXT("fill:false は輪郭のみ"), Ovals[1]->bFill);
	TestEqual(TEXT("輪郭幅"), Ovals[1]->Thickness, 2.f);
	return true;
}

// ===== スクロールバーの寸法（2026-09-20・設計 Docs/Reports/2026/09/20/05 §2.2）=====
// 描画（SKaedeUiCanvas::OnPaint）と当たり判定（ScrollBarAt）が同じ純関数を見ているので、
// ここで測れば両方の合否になる。**数値で書く**（「それらしい」では回帰にならない）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiScrollBarTest, "MomijiUI.ScrollBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiScrollBarTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	FKaedeScrollBar Bar;

	// 収まっていればバーは無い。
	TestFalse(TEXT("中身がビューポートに収まる → バー無し"),
		ScrollBarGeometry(100.f, 50.f, 400.f, 300.f, /*ContentH=*/280.f, 0.f, Bar));

	// 中身が2倍。つまみは半分の高さで、先頭では溝の上端に接する。
	TestTrue(TEXT("はみ出す → バーあり"),
		ScrollBarGeometry(100.f, 50.f, 400.f, 300.f, /*ContentH=*/600.f, 0.f, Bar));
	TestEqual(TEXT("スクロールできる量"), Bar.MaxScroll, 300.f);
	TestEqual(TEXT("溝の幅は 6"), Bar.TrackW, 6.f);
	TestEqual(TEXT("溝の X はビューポート右端 − 6 − 2"), Bar.TrackX, 492.f);
	TestEqual(TEXT("溝の高さ＝ビューポート高"), Bar.TrackH, 300.f);
	TestEqual(TEXT("掴める幅は 10（子が空けている幅と同じ）"), Bar.HitW, 10.f);
	TestEqual(TEXT("掴める帯の X はビューポート右端 − 10"), Bar.HitX, 490.f);
	TestEqual(TEXT("つまみの高さ＝300×(300/600)"), Bar.ThumbH, 150.f);
	TestEqual(TEXT("先頭ではつまみは溝の上端"), Bar.ThumbY, 50.f);

	// 末尾ではつまみの下端が溝の下端に接する。
	TestTrue(TEXT("末尾"), ScrollBarGeometry(100.f, 50.f, 400.f, 300.f, 600.f, /*Offset=*/300.f, Bar));
	TestEqual(TEXT("末尾のつまみ Y"), Bar.ThumbY, 200.f);
	TestEqual(TEXT("末尾はつまみ下端＝溝下端"), Bar.ThumbY + Bar.ThumbH, 350.f);

	// 中身が極端に長くても、つまみは最小 24px を割らない（掴めなくなるため）。
	TestTrue(TEXT("長い中身"), ScrollBarGeometry(0.f, 0.f, 400.f, 300.f, /*ContentH=*/30000.f, 0.f, Bar));
	TestEqual(TEXT("つまみの最小は 24"), Bar.ThumbH, 24.f);

	// クランプ: 範囲外の Offset を渡してもつまみは溝からはみ出さない。
	TestTrue(TEXT("範囲外"), ScrollBarGeometry(0.f, 0.f, 400.f, 300.f, 600.f, /*Offset=*/9999.f, Bar));
	TestEqual(TEXT("範囲外でも溝の下端で止まる"), Bar.ThumbY + Bar.ThumbH, 300.f);
	return true;
}

// ===== MomijiUI の照合（2026-10-03・仕様 Docs/Guides/Working/momiji-ui-framework.md §10）=====
// Plugins/MomijiUI/Fixtures/*.json（機能ごとのフィクスチャ）を、実際の字の計測（同梱の BIZ UDP ゴシック）で配置し、
// Saved/MomijiUIConformance/<id>.ue.json に書く:
//   hits   … action が "t:" の当たり（鍵・矩形・スクロール・焦点の群・上下左右の移動先）
//   prims  … 描画命令の全部（矩形・枠・文字（行ごと）・画像・線・楕円）。当たりの無い Label や Image の配置もここで比べる
//   scenarios … フィクスチャの "scenarios" を本物のキャンバス（SKaedeUiCanvas）で通した結果（各手の焦点の鍵・最前面の群・
//               スクロール量・押した鍵・測った高さ・文字の選択）
// ブラウザ側（Tools/momiji-ui/conformance.mjs）が同じフィクスチャで同じものを出し、Scripts/momiji-ui-conformance.ps1 が突き合わせる。
// **このテストは書き出せたかだけを見る**（合否は突き合わせの側）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMomijiUIConformanceTest, "MomijiUI.MomijiUIConformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMomijiUIConformanceTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FString Dir = MomijiUI::PluginDir() / TEXT("Fixtures");
	const FString OutDir = FPaths::ProjectSavedDir() / TEXT("MomijiUIConformance");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
	TestTrue(TEXT("fixtures exist"), Files.Num() > 0);
	if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate が無い（字を測れない）")); return false; }
	const FKaedeMeasureFn Measure = MakeSlateMeasure(16);   // 描画と同じ測り方（fontSize 0 は既定 16）
	const kaedevn::ui::UiTheme Theme = KaedeCore::BuiltinTheme("midnight");
	for (const FString& F : Files)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *(Dir / F))) { AddError(FString::Printf(TEXT("読めない: %s"), *F)); continue; }
		const std::string Src = TCHAR_TO_UTF8(*Json);
		const nlohmann::json J = nlohmann::json::parse(Src, nullptr, false);
		if (!J.is_object()) { AddError(FString::Printf(TEXT("JSON でない: %s"), *F)); continue; }
		// ---- 診断（code と path をブラウザと突き合わせる。invalid のフィクスチャは診断だけ）----
		nlohmann::json Diags = nlohmann::json::array();
		for (const kaedevn::ui::UiDiagnostic& D : KaedeCore::ValidateScreenJson(Src)) Diags.push_back({ {"severity", D.severity}, {"code", D.code}, {"path", D.path} });
		const FString Id = FPaths::GetBaseFilename(F);
		if (J.value("invalid", false))
		{
			const nlohmann::json Out = { {"diags", Diags} };
			TestTrue(FString::Printf(TEXT("wrote %s"), *Id), FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(Out.dump(1).c_str()), *(OutDir / (Id + TEXT(".ue.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			continue;
		}
		const kaedevn::ui::UiNode Pristine = KaedeCore::ScreenTreeFromJson(J);   // -KaedeUiShot と同じ読み方（1 回の parse から木と bind）
		const FKaedeScreenValues V = J.contains("bind") ? ReadScreenValues(J["bind"]) : FKaedeScreenValues{};

		// ---- 配置: 当たり（鍵）と描画命令 ----
		kaedevn::ui::UiNode Tree = Pristine;
		ApplyVarsToTree(Tree, V.Vars);
		KaedeCore::BindScreenTree(Tree, V.Strings, V.Vars, V.Lists);
		KaedeCore::ApplyEnabledTree(Tree, V.Vars);
		KaedeCore::PruneScreenTree(Tree, V.Vars);
		TArray<FKaedeHit> Hits;
		FKaedeUiSkin Skin; Skin.Theme = Theme;
		const TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure, &Hits, Skin, nullptr);
		const TArray<FKaedeFocusNav> Nav = BuildFocusNav(Hits);
		auto KeyAt = [&Hits](int32 I) { return std::string(Hits.IsValidIndex(I) ? TCHAR_TO_UTF8(*HitKey(Hits[I])) : ""); };
		nlohmann::json HitsOut = nlohmann::json::array();
		for (int32 I = 0; I < Hits.Num(); ++I)
		{
			const FKaedeHit& H = Hits[I];
			if (!H.Action.StartsWith(TEXT("t:"))) continue;
			HitsOut.push_back({ {"key", TCHAR_TO_UTF8(*HitKey(H))}, {"action", TCHAR_TO_UTF8(*H.Action)}, {"x", H.X}, {"y", H.Y}, {"w", H.W}, {"h", H.H},
			                    {"scroll", TCHAR_TO_UTF8(*H.ScrollId)}, {"group", H.FocusGroup},
			                    {"up", KeyAt(Nav[I].Up)}, {"down", KeyAt(Nav[I].Down)}, {"left", KeyAt(Nav[I].Left)}, {"right", KeyAt(Nav[I].Right)} });
		}

		// ---- 操作シナリオ: 本物のキャンバスで通す ----
		nlohmann::json ScnOut = nlohmann::json::array();
		if (J.contains("scenarios") && J["scenarios"].is_array())
		{
			for (const nlohmann::json& Sc : J["scenarios"])
			{
				TSharedRef<SKaedeUiCanvas> Canvas = SNew(SKaedeUiCanvas).LogicalW(1280).LogicalH(720);
				FKaedeScreenValues SV = V;
				// 画面の名前は "screen"（PresentScreen の既定）。手 "screen" で切り替える（画面ごとの状態・仕様 §6）
				FString ScreenName = TEXT("screen");
				auto Present = [&]() { PresentScreen(*Canvas, Pristine, SV, Theme, FLinearColor::Transparent, nullptr, ScreenName); };
				// 押された action の数（文字欄の Enter の送信も、焦点の部品の Enter も、クリックもここへ）。ブラウザは attach の onEvent の数
				int32 Events = 0;
				Canvas->SetOnAction([&Events](const FString&, float) { ++Events; });
				Present();
				nlohmann::json Steps = nlohmann::json::array();
				auto Str = [](const nlohmann::json& J, const char* Key) { return FString(UTF8_TO_TCHAR(J[Key].get<std::string>().c_str())); };
				auto Record = [&](nlohmann::json& R)
				{
					R["focus"] = TCHAR_TO_UTF8(*Canvas->FocusedKey());
					R["top"] = Canvas->GetTopFocusGroup();
					nlohmann::json Sc2 = nlohmann::json::object();
					for (const FString& ScrollId : Canvas->ScrollIds()) Sc2[TCHAR_TO_UTF8(*ScrollId)] = Canvas->GetScrollOffset(ScrollId);
					R["scroll"] = Sc2;
					// 編集器の状態（2026-10-04・Codex 10/04/01 F1〜F3）: 編集中の欄・画面に出ている欄の中身・押された数・編集器の見える矩形
					R["active"] = TCHAR_TO_UTF8(*Canvas->ActiveTextInput());
					R["events"] = Events;
					nlohmann::json In = nlohmann::json::object();
					for (const FString& FieldId : Canvas->TextInputIds()) In[TCHAR_TO_UTF8(*FieldId)] = TCHAR_TO_UTF8(*Canvas->GetTextInputValue(FieldId));
					R["inputs"] = In;
					FVector2f EP, ES;
					if (Canvas->TextEditorVisibleRect(EP, ES)) R["editor"] = { {"x", EP.X}, {"y", EP.Y}, {"w", ES.X}, {"h", ES.Y} };
					else R["editor"] = nullptr;
					// 文字の選択（2026-10-07）: 器・範囲（行, 字）・写す文。無ければ null
					SKaedeUiCanvas::FKaedeSelectionInfo Sel;
					if (Canvas->GetSelection(Sel))
						R["sel"] = { {"group", TCHAR_TO_UTF8(*Sel.Group)}, {"from", nlohmann::json::array({Sel.FromLine, Sel.FromCh})}, {"to", nlohmann::json::array({Sel.ToLine, Sel.ToCh})}, {"text", TCHAR_TO_UTF8(*Sel.Text)} };
					else R["sel"] = nullptr;
					Steps.push_back(R);
				};
				{ nlohmann::json R; Record(R); }
				for (const nlohmann::json& St : Sc.value("steps", nlohmann::json::array()))
				{
					nlohmann::json R;
					if (St.contains("move")) { Canvas->MoveFocus(St["move"][0].get<int32>(), St["move"][1].get<int32>()); }
					else if (St.contains("bind"))   // 手の bind は今の値へ上書きで足す
					{
						const FKaedeScreenValues Add = ReadScreenValues(St["bind"]);
						for (const auto& Kv : Add.Strings) SV.Strings[Kv.first] = Kv.second;
						for (const auto& Kv : Add.Vars) SV.Vars[Kv.first] = Kv.second;
						for (const auto& Kv : Add.Lists) SV.Lists[Kv.first] = Kv.second;
						Present();
					}
					else if (St.contains("scroll")) { Canvas->SetScrollOffset(Str(St["scroll"], "id"), St["scroll"]["y"].get<float>()); Present(); }
					else if (St.contains("hit")) { R["hit"] = TCHAR_TO_UTF8(*Canvas->HitKeyAt(FVector2f(St["hit"][0].get<float>(), St["hit"][1].get<float>()))); }
					// ---- 入力の手（本物の入力経路: OnMouseButtonDown/Up・SEditableText の文字の変化・Enter の確定）----
					else if (St.contains("click")) { Canvas->ClickAtProbe(FVector2f(St["click"][0].get<float>(), St["click"][1].get<float>())); }
					else if (St.contains("select"))   // 押す→動かす→離す（OnMouseButtonDown / Move / Up と同じ関数）
					{
						const nlohmann::json& P = St["select"];
						Canvas->SelectTextProbe(FVector2f(P[0][0].get<float>(), P[0][1].get<float>()), FVector2f(P[1][0].get<float>(), P[1][1].get<float>()));
					}
					else if (St.contains("probe"))
					{
						const FString FieldAt = Canvas->TextInputIdAt(FVector2f(St["probe"][0].get<float>(), St["probe"][1].get<float>()));
						R["under"] = FieldAt.IsEmpty() ? std::string("canvas") : std::string("input:") + TCHAR_TO_UTF8(*FieldAt);
					}
					else if (St.contains("type")) { Canvas->TypeIntoActiveTextInput(Str(St, "type")); }
					else if (St.contains("enter"))
					{
						// Enter: 編集中の欄があれば確定（送信）。無ければ host の規則＝方向キーで焦点を出している間だけ焦点の部品を押す（ブラウザの attach と同じ）
						if (!Canvas->ActiveTextInput().IsEmpty()) { Canvas->CommitActiveTextInput(); }
						else if (Canvas->IsFocusVisible() && !Canvas->FocusedAction().IsEmpty()) { ++Events; }
					}
					else if (St.contains("tab")) { /* Slate の Tab は編集器の外では何もしない */ }
					else if (St.contains("imeEnter")) { R["ime"] = "n/a"; }   // Slate の IME はこの手では動かさない（ブラウザだけ）
					else if (St.contains("screen")) { ScreenName = Str(St, "screen"); Present(); }
					else if (St.contains("reset")) { Canvas->ResetScreenState(Str(St, "reset")); Present(); }
					else if (St.contains("input")) { Canvas->SetTextInputValue(Str(St["input"], "id"), Str(St["input"], "value")); }
					else if (St.contains("measure"))
					{
						// 値を差し込んだ部品の高さを描かずに測る（template は item で 1 行ぶんに展開してから）
						kaedevn::ui::UiNode N = KaedeCore::LoadScreenTree(St["measure"]["node"].dump());   // 裸の部品 JSON も受ける
						if (St["measure"].contains("item"))
						{
							kaedevn::ui::ItemScope S;
							for (auto K = St["measure"]["item"].begin(); K != St["measure"]["item"].end(); ++K) S[K.key()] = K.value().is_string() ? K.value().get<std::string>() : K.value().dump();
							N = KaedeCore::InstantiateTemplate(N, S);
						}
						R["h"] = MeasureNodeHeight(N, Measure);
					}
					Record(R);
				}
				ScnOut.push_back({ {"name", Sc.value("name", std::string())}, {"steps", Steps} });
			}
		}

		nlohmann::json Out = { {"hits", HitsOut}, {"prims", PrimsOf(Cmds)}, {"scenarios", ScnOut}, {"diags", Diags} };
		const bool bOk = FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(Out.dump(1).c_str()), *(OutDir / (Id + TEXT(".ue.json"))),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		TestTrue(FString::Printf(TEXT("wrote %s"), *Id), bOk);
	}
	return true;
}

// ===== 編集器（SEditableText）は ScrollView の見える範囲で切られ、枠外は Slate の当たり判定にも出ない（2026-10-04・Codex 10/04/01 F2・AC03）=====
// 半分だけ見える TextInput を編集中にし、本物の描画（Paint）を当たり判定の格子へ通してから、見える点と枠外の点で
// 「その点の下に SEditableText がいるか」を聞く。Slate は描いたときのクリップで当たりを判定する（HittestGrid の GetCurrentClippingState）ので、
// OnPaint が編集器を ScrollView の範囲で切っていれば枠外には出ない。キャンバスの TextFieldAt（押下が編集に入るか）も同じ 2 点で見る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiTextEditorClipTest, "MomijiUI.TextEditorClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiTextEditorClipTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate が無い")); return false; }
	// editor-scope.json の ScrollView と同じ: 箱 y 200..300、欄は中身の y 270..318（下端で 30px だけ見える）
	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"variant":"spacer","children":[
  {"type":"ScrollView","id":"list","x":40,"y":200,"w":400,"h":100,"pad":0,"gap":10,"children":[
    {"type":"Label","text":"filler","h":60},
    {"type":"TextInput","id":"inlist","bindVar":"inlist","action":"t:savelist","h":48,"placeholder":"in list"},
    {"type":"Label","text":"filler","h":60},
    {"type":"Button","id":"after","text":"after","action":"t:after","h":40}
  ]}
]}
)JSON";
	const kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	const TSharedRef<SKaedeUiCanvas> Canvas = SNew(SKaedeUiCanvas).LogicalW(1280).LogicalH(720);
	// 窓の飾り（題名の帯・縁）を付けず、キャンバスが窓の 1280×720 いっぱいに載る＝論理座標がそのまま窓の座標。
	// 当たり判定の経路は窓まで遡れないと空になる（FHittestGrid::GetBubblePath は末尾が窓でなければ捨てる）ので、窓ごと描く（PaintWindow）
	const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(1280, 720)).ScreenPosition(FVector2D(0, 0))
		.CreateTitleBar(false).UseOSWindowBorder(true).SizingRule(ESizingRule::FixedSize)[Canvas];
	FSlateApplication::Get().AddWindow(Window, false);
	ON_SCOPE_EXIT { FSlateApplication::Get().RequestDestroyWindow(Window); };
	FKaedeScreenValues V;
	const kaedevn::ui::UiTheme Theme = KaedeCore::BuiltinTheme("midnight");
	PresentScreen(*Canvas, Tree, V, Theme);

	// 本物の描画（窓ごと）を窓の当たり判定の格子へ通し、点の下に SEditableText がいるかを聞く
	auto PathAt = [&](float X, float Y) -> TArray<FWidgetAndPointer>
	{
		FSlateWindowElementList Elements(Window);
		Window->SlatePrepass(1.f);
		Window->PaintWindow(FPlatformTime::Seconds(), 0.f, Elements, FWidgetStyle(), true);
		// 格子は画面（デスクトップ）の座標。窓は (0,0) に頼めても、モニタの並びで OS が別の場所へ置く
		//（2026-10-12: 上にモニタがある PC で (320,-924) に置かれ、窓の外を聞いて空だった）ので、窓の実際の位置を足す
		const FVector2f WinPos(Window->GetPositionInScreen());
		return Window->GetHittestGrid().GetBubblePath(WinPos + FVector2f(X, Y), 0.f, /*bIgnoreEnabledStatus*/ true, 0);
	};
	auto EditorUnder = [&](float X, float Y) -> bool
	{
		for (const FWidgetAndPointer& W : PathAt(X, Y)) { if (W.Widget->GetType() == FName(TEXT("SEditableText"))) return true; }
		return false;
	};
	auto Describe = [&](float X, float Y) -> FString
	{
		FString S;
		for (const FWidgetAndPointer& W : PathAt(X, Y)) { S += W.Widget->GetType().ToString() + TEXT(","); }
		return S;
	};
	// 見える点で押す → 編集に入る。枠外の点（箱の下端 300 より下）は欄に当たらない
	TestEqual(TEXT("見える点の下は欄（TextFieldAt）"), Canvas->TextInputIdAt(FVector2f(240.f, 285.f)), FString(TEXT("inlist")));
	TestEqual(TEXT("枠外の点の下は欄でない（TextFieldAt）"), Canvas->TextInputIdAt(FVector2f(240.f, 310.f)), FString());
	Canvas->ClickAtProbe(FVector2f(240.f, 285.f));
	TestEqual(TEXT("見える点を押すと編集に入る"), Canvas->ActiveTextInput(), FString(TEXT("inlist")));
	FVector2f EP, ES;
	TestTrue(TEXT("編集器の見える矩形がある"), Canvas->TextEditorVisibleRect(EP, ES));
	TestEqual(TEXT("見える矩形 y=270（箱の中だけ）"), EP.Y, 270.f);
	TestEqual(TEXT("見える矩形 h=30（箱の下端まで）"), ES.Y, 30.f);
	AddInfo(FString::Printf(TEXT("path(240,285)=[%s] path(240,310)=[%s] editorVisible=%d"), *Describe(240.f, 285.f), *Describe(240.f, 310.f), Canvas->IsTextEditorFocused() ? 1 : 0));
	TestTrue(TEXT("Slate: 見える点 (240,285) の下に SEditableText がいる"), EditorUnder(240.f, 285.f));
	TestFalse(TEXT("Slate: 枠外の点 (240,310) の下に SEditableText はいない（クリップで切れている）"), EditorUnder(240.f, 310.f));
	// 完全に隠すと編集は降りる（AC04）
	Canvas->SetScrollOffset(TEXT("list"), 138.f);
	TestEqual(TEXT("隠れたら編集は降りる"), Canvas->ActiveTextInput(), FString());
	TestFalse(TEXT("Slate: 隠れたら SEditableText はどこにもいない"), EditorUnder(240.f, 285.f));
	return true;
}

// ===== 説明書の見本が動くこと（2026-10-03・Docs/Guides/Manuals/momiji-ui-manual.md §4）=====
// SMomijiUICounterExample（説明書に載せたコードそのもの）を作り、action を送って、状態と押せる部品が変わることを見る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiManualExampleTest, "MomijiUI.ManualExample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiManualExampleTest::RunTest(const FString& Parameters)
{
	TSharedRef<SMomijiUICounterExample> W = SNew(SMomijiUICounterExample).ScreenPath(MomijiUI::PluginDir() / TEXT("Examples/counter.json"));
	auto HasHit = [&W](const TCHAR* Action)
	{
		for (const KaedeUI::FKaedeHit& H : W->GetCanvas()->GetHits()) { if (H.Action == Action) return true; }
		return false;
	};
	TestTrue(TEXT("+1 は押せる"), HasHit(TEXT("count:add")));
	TestFalse(TEXT("count 0 ではリセットは押せない（enabledIf）"), HasHit(TEXT("count:reset")));
	TestTrue(TEXT("文字欄がある"), W->GetCanvas()->HasTextInput(TEXT("name")));
	// 動き: 「+1」は count == 0 の間だけ脈動する（anim.pulse の whileIf・鍵は 画面名/action）
	kaedevn::ui::UiMotionSample Sample;
	TestTrue(TEXT("押す前は +1 が脈動している"), W->GetMotion().sample("counter/count:add", Sample));

	W->HandleAction(TEXT("count:add"));
	W->HandleAction(TEXT("count:add"));
	TestEqual(TEXT("2 回押した"), W->GetCount(), 2);
	TestFalse(TEXT("押したら脈動は止まる"), W->GetMotion().sample("counter/count:add", Sample));
	TestTrue(TEXT("count 2 ではリセットが押せる"), HasHit(TEXT("count:reset")));

	W->GetCanvas()->SetTextInputValue(TEXT("name"), TEXT("霧走"));
	W->HandleAction(TEXT("name:add"));
	TestEqual(TEXT("名前が 1 つ"), W->GetNames().Num(), 1);
	TestEqual(TEXT("文字欄は空に戻る"), W->GetCanvas()->GetTextInputValue(TEXT("name")), FString());

	W->HandleAction(TEXT("count:reset"));
	TestEqual(TEXT("リセット"), W->GetCount(), 0);
	TestFalse(TEXT("リセット後は押せない"), HasHit(TEXT("count:reset")));
	return true;
}

// ===== リポジトリの画面 JSON を全部診断する（2026-10-03・仕様 §12）=====
// <画面の置き場>/**/*.json と、その隣の adv*/ui/*.json に error が無いこと（このリポジトリでは Game/ui と Game/adv*/ui）。warn（知らない鍵・名前の色）は数を出すだけ（直す対象の一覧は
// node Tools/momiji-ui/validate.mjs で場所つきに出る）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMomijiUIValidateTest, "MomijiUI.MomijiUIValidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMomijiUIValidateTest::RunTest(const FString& Parameters)
{
	// 見る範囲は node Tools/momiji-ui/validate.mjs と同じ: Game/ui/**/*.json と Game/adv*/ui/*.json（FindFilesRecursive は絶対パスを返す）
	// 置き場はプロジェクトの設定 [MomijiUI] ScreenRoot（= Game/ui）から求め、作品の画面はその親の adv*/ui を見る
	const FString ScreenRoot = MomijiUI::ProjectScreenRoot();
	const FString WorksParent = FPaths::GetPath(ScreenRoot);
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *ScreenRoot, TEXT("*.json"), true, false);
	TArray<FString> Works;
	IFileManager::Get().FindFiles(Works, *(WorksParent / TEXT("adv*")), false, true);
	for (const FString& W : Works)
		IFileManager::Get().FindFilesRecursive(Files, *(WorksParent / W / TEXT("ui")), TEXT("*.json"), true, false, /*bClearFileNames*/ false);
	int32 Screens = 0, Errors = 0, Warns = 0;
	for (const FString& Path : Files)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path)) continue;
		const nlohmann::json J = nlohmann::json::parse(TCHAR_TO_UTF8(*Json), nullptr, false);
		if (!J.is_object() || (!J.contains("root") && !J.contains("type"))) continue;   // 画面でない JSON（manifest 等）
		if (Path.Contains(TEXT("momiji-ui-conformance"))) continue;                       // 照合のフィクスチャ（invalid がある）
		++Screens;
		for (const kaedevn::ui::UiDiagnostic& D : KaedeCore::ValidateScreenJson(TCHAR_TO_UTF8(*Json)))
		{
			if (D.severity == "error") { ++Errors; AddError(FString::Printf(TEXT("%s%s: %s"), *Path, UTF8_TO_TCHAR(D.path.c_str()), UTF8_TO_TCHAR(D.message.c_str()))); }
			else ++Warns;
		}
	}
	AddInfo(FString::Printf(TEXT("screens=%d errors=%d warns=%d"), Screens, Errors, Warns));
	TestTrue(TEXT("screens found"), Screens > 0);
	return Errors == 0;
}

// ===== 枠の色と太さ（borderColor・borderWidth・2026-10-12）=====
// 線だけの四角（地が透明の Panel）・色の Image の枠・枠なし（borderWidth 0）・並べる中でも効く・選んでいる間は accent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiBorderTest, "MomijiUI.Border",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiBorderTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"variant":"spacer","children":[
  {"type":"Panel","id":"line","x":100,"y":100,"w":300,"h":150,"color":"#00000000","borderColor":"#ff0000","borderWidth":3},
  {"type":"Image","id":"img","x":500,"y":100,"w":200,"h":100,"color":"#203040","borderColor":"#00ff00","borderWidth":2},
  {"type":"Panel","id":"none","x":800,"y":100,"w":200,"h":100,"color":"#405060","borderWidth":0},
  {"type":"VerticalLayout","x":100,"y":400,"w":400,"h":200,"pad":0,"gap":0,"children":[
    {"type":"HorizontalLayout","h":60,"variant":"dark","borderColor":"#0000ff","borderWidth":4,"children":[]},
    {"type":"HorizontalLayout","h":60,"variant":"dark","borderColor":"#0000ff","highlight":true,"children":[]}
  ]}
]}
)JSON";
	const kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	const FKaedeMeasureFn Measure = FixedMeasure();
	const TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure);
	// 枠の太さ（2026-10-12 から太い枠は 1 本の太い線。同じ色の枠の線の太さを足す）
	auto Outlines = [&](const FColor& C, float X0, float Y0, float X1, float Y1)
	{
		int32 N = 0;
		for (const FKaedeDrawCmd& D : Cmds)
			if (D.Kind == EKaedeDrawKind::RectOutline && D.Color == C && D.X >= X0 && D.Y >= Y0 && D.X < X1 && D.Y < Y1) N += FMath::RoundToInt(D.Thickness);
		return N;
	};
	auto AnyOutline = [&](float X0, float Y0, float X1, float Y1)
	{
		for (const FKaedeDrawCmd& D : Cmds)
			if (D.Kind == EKaedeDrawKind::RectOutline && D.X >= X0 && D.Y >= Y0 && D.X < X1 && D.Y < Y1) return true;
		return false;
	};
	TestEqual(TEXT("線だけの四角: 赤い枠の太さ 3"), Outlines(FColor(255, 0, 0, 255), 90, 90, 410, 260), 3);
	bool bFillVisible = false;
	for (const FKaedeDrawCmd& D : Cmds)
		if (D.Kind == EKaedeDrawKind::Rect && D.X == 100 && D.Y == 100 && D.Color.A > 0) bFillVisible = true;
	TestFalse(TEXT("線だけの四角: 地は透明"), bFillVisible);
	TestEqual(TEXT("色の Image: 緑の枠の太さ 2"), Outlines(FColor(0, 255, 0, 255), 490, 90, 710, 210), 2);
	TestFalse(TEXT("borderWidth 0: 枠なし"), AnyOutline(790, 90, 1010, 210));
	TestEqual(TEXT("並べる中の入れ物: 青い枠の太さ 4"), Outlines(FColor(0, 0, 255, 255), 90, 390, 510, 455), 4);
	TestEqual(TEXT("選んでいる間は accent（青い枠は出ない）"), Outlines(FColor(0, 0, 255, 255), 90, 455, 510, 530), 0);
	return true;
}


// ===== 2026-10-12 の描き方（rpg-themes/README.md §3）: 描画命令で確かめる =====
// 2026-10-18（Issue #76）: 25 の確認を 1 本に束ねていたのを、地・文字・変形・部品・テーマの 5 本に分けた（落ちたとき名前で機能が分かる）。
namespace
{
	// 描き方の見本（地の濃淡・形・立体の縁・文字の縁取りと字間と大小・ずらす・回す・薄く・区切り線・棒・絵の 9 分割と切り出し・多角形）
	const char* kThemeDrawingJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"variant":"spacer","children":[
  {"type":"Panel","id":"grad","x":10,"y":10,"w":100,"h":50,"gradient":{"dir":"v","stops":[["#ff0000",0],["#0000ff",1]]}},
  {"type":"Panel","id":"hex","x":200,"y":10,"w":100,"h":40,"shape":"hex","color":"#00ff00"},
  {"type":"Panel","id":"e3d","x":400,"y":10,"w":100,"h":60,"color":"#202020","edge3d":{"light":"#fafafa","dark":"#505050","width":2}},
  {"type":"Label","x":10,"y":100,"w":300,"h":30,"text":"AB","textOutline":{"width":3,"color":"#000000"}},
  {"type":"Label","x":10,"y":140,"w":300,"h":30,"text":"ABC","letterSpacing":5},
  {"type":"Label","x":10,"y":180,"w":300,"h":40,"runs":[{"text":"12","size":24},{"text":"/34","size":12}]},
  {"type":"Panel","x":600,"y":100,"w":100,"h":40,"color":"#ffffff","offset":[30,20]},
  {"type":"Panel","x":600,"y":200,"w":100,"h":40,"color":"#ffffff","rotate":-8},
  {"type":"Panel","x":800,"y":200,"w":100,"h":40,"color":"#ffffffff","opacity":0.5},
  {"type":"Divider","x":10,"y":260,"w":300,"h":12,"ornament":"diamond"},
  {"type":"Slider","x":10,"y":300,"w":200,"h":10,"value":0.5,"gradient":{"dir":"h","stops":[["#ff0000",0],["#00ff00",1]]}},
  {"type":"Image","x":10,"y":330,"w":200,"h":100,"sprite":"x.png","slice":[8,8,8,8]},
  {"type":"Image","x":300,"y":330,"w":24,"h":16,"sprite":"x.png","src":[24,0,24,16]},
  {"type":"Polygon","x":600,"y":330,"w":100,"h":60,"points":[[0,0],[100,10],[80,60],[10,50]],"color":"#123456"}
]}
)JSON";

	// 見本を 1 字 10px で配置した描画命令
	TArray<FKaedeDrawCmd> ThemeDrawingCmds()
	{
		return ComputeDrawList(KaedeCore::LoadScreenTree(kThemeDrawingJson), 1280, 720, FixedMeasure(10.f));
	}
}

// 地: gradient（濃淡）・shape（形）・edge3d（立体の縁）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiThemeDrawingFillTest, "MomijiUI.ThemeDrawing.Fill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiThemeDrawingFillTest::RunTest(const FString& Parameters)
{
	const TArray<FKaedeDrawCmd> Cmds = ThemeDrawingCmds();
	const FKaedeDrawCmd* G = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Gradient && C.X == 10 && C.Y == 10; });
	TestTrue(TEXT("gradient: 濃淡の命令（2 色・縦）"), G && G->GradColors.Num() == 2 && !G->bGradH && G->GradColors[0] == FColor(255, 0, 0, 255));
	TestTrue(TEXT("shape hex: 6 頂点の多角形"), CountCmds(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Polygon && C.Points.Num() == 6 && C.X == 200; }) == 1);
	TestEqual(TEXT("edge3d: 明るい縁 2 本"), CountCmds(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.Color == FColor(0xfa, 0xfa, 0xfa, 255); }), 2);
	TestEqual(TEXT("edge3d: 暗い縁 2 本"), CountCmds(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.Color == FColor(0x50, 0x50, 0x50, 255); }), 2);
	return true;
}

// 文字: textOutline（縁取り）・letterSpacing（字間）・runs の size（大小と下揃え）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiThemeDrawingTextTest, "MomijiUI.ThemeDrawing.Text",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiThemeDrawingTextTest::RunTest(const FString& Parameters)
{
	const TArray<FKaedeDrawCmd> Cmds = ThemeDrawingCmds();
	const FKaedeDrawCmd* T1 = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("AB"); });
	TestTrue(TEXT("textOutline: 縁取りの太さ 3"), T1 && T1->OutlinePx == 3.f);
	TestEqual(TEXT("letterSpacing: 1 字ずつ（A B C）"), CountCmds(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Y == 140 && C.Text.Len() == 1; }), 3);
	const FKaedeDrawCmd* LA = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("A") && C.Y == 140; });
	const FKaedeDrawCmd* LB = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("B") && C.Y == 140; });
	TestTrue(TEXT("letterSpacing: B は A から 10+5"), LA && LB && FMath::IsNearlyEqual(LB->X - LA->X, 15.f, 0.01f));
	const FKaedeDrawCmd* R1 = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("12"); });
	const FKaedeDrawCmd* R2 = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("/34"); });
	TestTrue(TEXT("runs size: 24 と 12"), R1 && R2 && R1->FontPt == 24.f && R2->FontPt == 12.f);
	TestTrue(TEXT("runs size: 下を揃える（小さい方が下に）"), R1 && R2 && R2->Y > R1->Y);
	return true;
}

// 変形: offset（ずらす）・rotate（回す）・opacity（薄く）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiThemeDrawingTransformTest, "MomijiUI.ThemeDrawing.Transform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiThemeDrawingTransformTest::RunTest(const FString& Parameters)
{
	const TArray<FKaedeDrawCmd> Cmds = ThemeDrawingCmds();
	TestTrue(TEXT("offset: 30,20 ずれる"), FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.X == 630 && C.Y == 120; }) != nullptr);
	const FKaedeDrawCmd* Rot = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.X == 600 && C.Y == 200; });
	TestTrue(TEXT("rotate: 角度と軸（中心）"), Rot && Rot->Angle == -8.f && Rot->XfX == 650.f && Rot->XfY == 220.f);
	const FKaedeDrawCmd* Op = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.X == 800 && C.Y == 200; });
	TestTrue(TEXT("opacity: 半分"), Op && Op->Color.A >= 126 && Op->Color.A <= 129);
	return true;
}

// 部品: Divider（区切り線の飾り）・Slider の棒・Image の slice と src・Polygon
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiThemeDrawingPartsTest, "MomijiUI.ThemeDrawing.Parts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiThemeDrawingPartsTest::RunTest(const FString& Parameters)
{
	const TArray<FKaedeDrawCmd> Cmds = ThemeDrawingCmds();
	TestEqual(TEXT("Divider diamond: 菱形 3 つ"), CountCmds(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Polygon && C.Points.Num() == 4 && C.Y > 250 && C.Y < 280; }), 3);
	const FKaedeDrawCmd* Bar = FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Gradient && C.Y == 300; });
	TestTrue(TEXT("Slider: 棒は横の濃淡・長さは半分・tween の印"), Bar && Bar->bGradH && Bar->W == 100.f && Bar->bFillBar);
	TestTrue(TEXT("Image slice"), FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Image && C.Slice[0] == 8.f && C.Fit == TEXT("stretch"); }) != nullptr);
	TestTrue(TEXT("Image src"), FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Image && C.Src[0] == 24.f && C.Src[2] == 24.f; }) != nullptr);
	TestTrue(TEXT("Polygon: 部品の左上から"), FindCmd(Cmds, [](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Polygon && C.Points.Num() == 4 && C.Points[1] == FVector2f(700.f, 340.f); }) != nullptr);
	return true;
}

// テーマ: knockout（半透明の窓どうしの重なりで、後ろの窓の地を描かない）と select（選んだ行の地・文字・カーソル）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiThemeDrawingThemeTest, "MomijiUI.ThemeDrawing.Theme",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiThemeDrawingThemeTest::RunTest(const FString& Parameters)
{
	const char* kWin = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"variant":"spacer","children":[
  {"type":"VerticalLayout","x":100,"y":100,"w":400,"h":200,"variant":"dark","children":[]},
  {"type":"VerticalLayout","x":300,"y":200,"w":400,"h":200,"variant":"dark","children":[
    {"type":"HorizontalLayout","h":40,"highlight":true,"children":[{"type":"Label","text":"Row","h":40,"flex":1}]}]}
]}
)JSON";
	kaedevn::ui::UiTheme Th = KaedeCore::LoadTheme(R"({"track":"#00000080","knockout":true,"radius":0,
		"select":{"fill":"#336699","text":"#ffff00","cursor":{"sprite":"hand.png","w":30,"h":20,"dx":-34}}})");
	FKaedeUiSkin Skin; Skin.Theme = Th;
	const TArray<FKaedeDrawCmd> W = ComputeDrawList(KaedeCore::LoadScreenTree(kWin), 1280, 720, FixedMeasure(10.f), nullptr, Skin, nullptr);
	const int32 BackPieces = CountCmds(W, [](const FKaedeDrawCmd& C)   // 後ろの窓の中に収まる片だけ
		{ return C.Kind == EKaedeDrawKind::Rect && C.Color == FColor(0, 0, 0, 0x80) && C.X >= 100 && C.X + C.W <= 500 && C.Y >= 100 && C.Y + C.H <= 300; });
	TestEqual(TEXT("knockout: 後ろの窓は重なりを除いた 2 片"), BackPieces, 2);
	TestTrue(TEXT("select: 選んだ行の地"), W.ContainsByPredicate([](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Rect && C.Color == FColor(0x33, 0x66, 0x99, 255); }));
	TestTrue(TEXT("select: 選んだ行の文字の色"), W.ContainsByPredicate([](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Text && C.Text == TEXT("Row") && C.Color == FColor(255, 255, 0, 255); }));
	TestTrue(TEXT("select: カーソルの絵が行の左の外"), W.ContainsByPredicate([](const FKaedeDrawCmd& C) { return C.Kind == EKaedeDrawKind::Image && C.Sprite == TEXT("hand.png") && C.X < 300; }));
	return true;
}

// マウスを乗せた項目へ焦点が移る（SetHoverMovesFocus・RPG のゲームの画面。2026-10-12）。既定は今までどおり（乗せると焦点の印を消す）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiHoverMovesFocusTest, "MomijiUI.HoverMovesFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiHoverMovesFocusTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const FKaedeMeasureFn Measure = FixedMeasure();
	const char* kJson = R"JSON(
{ "type":"Panel","x":0,"y":0,"w":1280,"h":720,"variant":"spacer","children":[
  { "type":"Button","text":"はじめから","action":"newgame","x":100,"y":100,"w":200,"h":40 },
  { "type":"Button","text":"つづきから","action":"continue","x":100,"y":160,"w":200,"h":40 } ] }
)JSON";
	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TArray<FKaedeHit> Hits;
	TArray<FKaedeDrawCmd> Cmds = ComputeDrawList(Tree, 1280, 720, Measure, &Hits);
	const FGeometry Geo = FGeometry::MakeRoot(FVector2f(1280.f, 720.f), FSlateLayoutTransform());
	auto Move = [&](const TSharedRef<SKaedeUiCanvas>& C, float X, float Y)
	{
		const FPointerEvent Ev(0, FVector2D(X, Y), FVector2D(X, Y), TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		C->OnMouseMove(Geo, Ev);
	};
	{
		const TSharedRef<SKaedeUiCanvas> C = SNew(SKaedeUiCanvas).LogicalW(1280).LogicalH(720);
		C->SetContent(Cmds, Hits, FLinearColor::Black);
		C->SetFocusVisible(true);
		C->SetHoverMovesFocus(true);
		TestEqual(TEXT("はじめは先頭"), C->FocusedAction(), FString(TEXT("newgame")));
		Move(C, 200.f, 180.f);
		TestEqual(TEXT("乗せた項目に焦点が移る"), C->FocusedAction(), FString(TEXT("continue")));
		TestTrue(TEXT("焦点の印は出たまま（印は 1 つ）"), C->IsFocusVisible());
		Move(C, 900.f, 600.f);
		TestEqual(TEXT("項目の無い所では動かない"), C->FocusedAction(), FString(TEXT("continue")));
	}
	{
		const TSharedRef<SKaedeUiCanvas> C = SNew(SKaedeUiCanvas).LogicalW(1280).LogicalH(720);
		C->SetContent(Cmds, Hits, FLinearColor::Black);
		C->SetFocusVisible(true);
		Move(C, 200.f, 180.f);
		TestFalse(TEXT("既定: マウスを乗せると焦点の印を消す（今までどおり）"), C->IsFocusVisible());
	}
	return true;
}

// 部品ごとの矩形 OutRects（2026-10-16・UI エディタ U3。Docs/Guides-rpg/Working/rpg-ui-editor-design.md §5.2）。
// 見本の木（{root} の包み・repeat・showIf で刈り込む部品）で:
//   矩形の数＝刈り込み後の部品の数・Path は元の JSON の場所・子の矩形が親に含まれる・repeat の複製が TemplatePath を持つ・
//   OutRects を渡さない呼び方の描画命令と当たりが、渡した呼び方と同じ（既存の描画を変えない）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiNodeRectsTest, "MomijiUI.NodeRects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiNodeRectsTest::RunTest(const FString& Parameters)
{
	using namespace KaedeUI;
	const char* kJson = R"JSON(
{ "id":"rects", "root": { "type":"Panel","x":0,"y":0,"w":1280,"h":720,"children":[
  { "type":"Label","text":"題","x":40,"y":20,"w":400,"h":32 },
  { "type":"VerticalLayout","x":40,"y":80,"w":600,"h":400,"pad":8,"gap":4,"children":[
    { "type":"Button","text":"A","h":40,"action":"t:a","id":"a" },
    { "type":"Label","text":"隠す","showIf":"shown" },
    { "type":"VerticalLayout","repeat":"rows","template":
      { "type":"HorizontalLayout","h":36,"children":[
        { "type":"Label","text":"{item.name}","w":200 },
        { "type":"Label","text":"{item.hp}","w":80 } ] } }
  ]},
  { "type":"Panel","x":700,"y":80,"w":500,"h":300,"children":[
    { "type":"Label","text":"右","x":720,"y":100,"w":200,"h":28 } ] }
]}}
)JSON";
	// 字の計測は固定幅（Slate に依らない。矩形は字の幅に依らない形にしてある）
	const FKaedeMeasureFn Measure = FixedMeasure(16.f);

	// 読み込みが srcPath を付ける（包みあり → /root…・template の中 → …/template/…）
	kaedevn::ui::UiNode Tree = KaedeCore::LoadScreenTree(kJson);
	TestEqual(TEXT("root srcPath"), FString(UTF8_TO_TCHAR(Tree.srcPath.c_str())), FString(TEXT("/root")));
	TestEqual(TEXT("child srcPath"), FString(UTF8_TO_TCHAR(Tree.children[1].children[0].srcPath.c_str())), FString(TEXT("/root/children/1/children/0")));
	if (TestTrue(TEXT("template exists"), (bool)Tree.children[1].children[2].repeatTemplate))
		TestEqual(TEXT("template srcPath"), FString(UTF8_TO_TCHAR(Tree.children[1].children[2].repeatTemplate->children[1].srcPath.c_str())),
		          FString(TEXT("/root/children/1/children/2/template/children/1")));
	// 包みの無い木は /children/…
	{
		const kaedevn::ui::UiNode Bare = KaedeCore::LoadScreenTree(R"({"type":"Panel","children":[{"type":"Label"},{"type":"Panel","children":[{"type":"Label"}]}]})");
		TestEqual(TEXT("bare root srcPath"), FString(UTF8_TO_TCHAR(Bare.srcPath.c_str())), FString());
		TestEqual(TEXT("bare child srcPath"), FString(UTF8_TO_TCHAR(Bare.children[1].children[0].srcPath.c_str())), FString(TEXT("/children/1/children/0")));
	}

	// 値を差し込んで刈り込む（repeat 3 行・shown=0 で Label 1 つが消える）
	std::unordered_map<std::string, std::vector<kaedevn::ui::ItemScope>> Lists;
	Lists["rows"] = { {{"name", "Alma"}, {"hp", "30"}}, {{"name", "Bea"}, {"hp", "12"}}, {{"name", "Cid"}, {"hp", "7"}} };
	const std::unordered_map<std::string, double> Vars = { {"shown", 0.0} };
	KaedeCore::BindScreenTree(Tree, {}, Vars, Lists);
	KaedeCore::ApplyEnabledTree(Tree, Vars);
	KaedeCore::PruneScreenTree(Tree, Vars);
	const int32 NodeCount = CountNodes(Tree);
	TestEqual(TEXT("pruned tree node count"), NodeCount, 16);   // 根・題・縦・A・repeat・3 行×（横・名・HP）・右の窓・右

	const kaedevn::ui::UiTheme Theme = KaedeCore::BuiltinTheme("midnight");
	TArray<FKaedeHit> HitsA, HitsB;
	TArray<FKaedeNodeRect> Rects;
	const TArray<FKaedeDrawCmd> WithRects = ComputeDrawList(Tree, 1280, 720, Measure, &HitsA, &Theme, &Rects);
	const TArray<FKaedeDrawCmd> Without = ComputeDrawList(Tree, 1280, 720, Measure, &HitsB, &Theme);

	// 矩形の数＝刈り込み後の部品の数
	TestEqual(TEXT("rect count == node count"), Rects.Num(), NodeCount);
	if (Rects.Num() != NodeCount) return false;

	// 根は深さ 0・Path は元の JSON の場所
	TestEqual(TEXT("root depth"), Rects[0].Depth, 0);
	TestEqual(TEXT("root path"), Rects[0].Path, FString(TEXT("/root")));
	const FKaedeNodeRect* Btn = Rects.FindByPredicate([](const FKaedeNodeRect& R) { return R.Path == TEXT("/root/children/1/children/0"); });
	if (TestNotNull(TEXT("button rect"), Btn))
	{
		TestEqual(TEXT("button x"), Btn->X, 48.f);
		TestEqual(TEXT("button y"), Btn->Y, 88.f);
		TestEqual(TEXT("button w"), Btn->W, 584.f);
		TestEqual(TEXT("button h"), Btn->H, 40.f);
		TestFalse(TEXT("button not from repeat"), Btn->bFromRepeat);
		// 当たりと同じ矩形
		const FKaedeHit* H = HitsA.FindByPredicate([](const FKaedeHit& X) { return X.Action == TEXT("t:a"); });
		if (TestNotNull(TEXT("button hit"), H))
			TestTrue(TEXT("button rect == hit"), H->X == Btn->X && H->Y == Btn->Y && H->W == Btn->W && H->H == Btn->H);
	}
	TestNull(TEXT("pruned label has no rect"), Rects.FindByPredicate([](const FKaedeNodeRect& R) { return R.Path == TEXT("/root/children/1/children/1"); }));

	// 子の矩形が親に含まれる（描く順＝親が先。親は直前にある 1 段浅い矩形）
	for (int32 I = 1; I < Rects.Num(); ++I)
	{
		const FKaedeNodeRect& C = Rects[I];
		int32 P = I - 1;
		while (P >= 0 && Rects[P].Depth != C.Depth - 1) --P;
		if (!TestTrue(FString::Printf(TEXT("parent of %s"), *C.Path), P >= 0)) continue;
		const FKaedeNodeRect& Pa = Rects[P];
		const bool bIn = C.X >= Pa.X && C.Y >= Pa.Y && C.X + C.W <= Pa.X + Pa.W && C.Y + C.H <= Pa.Y + Pa.H;
		TestTrue(FString::Printf(TEXT("%s (%g,%g,%g,%g) inside %s (%g,%g,%g,%g)"), *C.Path, C.X, C.Y, C.W, C.H, *Pa.Path, Pa.X, Pa.Y, Pa.W, Pa.H), bIn);
	}

	// repeat の複製（行とその中身）は bFromRepeat・TemplatePath に元の template の Path
	const FString Tpl = TEXT("/root/children/1/children/2/template");
	int32 FromRepeat = 0, RowRoots = 0;
	for (const FKaedeNodeRect& R : Rects)
	{
		if (!R.bFromRepeat) { TestTrue(FString::Printf(TEXT("no template path: %s"), *R.Path), R.TemplatePath.IsEmpty()); continue; }
		++FromRepeat;
		TestEqual(FString::Printf(TEXT("template path of %s"), *R.Path), R.TemplatePath, Tpl);
		TestTrue(FString::Printf(TEXT("path inside template: %s"), *R.Path), R.Path.StartsWith(Tpl));
		if (R.Path == Tpl) ++RowRoots;
	}
	TestEqual(TEXT("from-repeat rects"), FromRepeat, 9);
	TestEqual(TEXT("repeat rows"), RowRoots, 3);

	// OutRects を渡さない呼び方の結果が同じ
	TestEqual(TEXT("same cmd count"), Without.Num(), WithRects.Num());
	TestEqual(TEXT("same hit count"), HitsB.Num(), HitsA.Num());
	if (Without.Num() == WithRects.Num())
		for (int32 I = 0; I < Without.Num(); ++I)
		{
			const FKaedeDrawCmd& A = WithRects[I];
			const FKaedeDrawCmd& B = Without[I];
			if (A.Kind != B.Kind || A.X != B.X || A.Y != B.Y || A.W != B.W || A.H != B.H || A.Text != B.Text || A.Color != B.Color || A.HitIndex != B.HitIndex)
			{
				AddError(FString::Printf(TEXT("cmd %d differs"), I));
				break;
			}
		}
	if (HitsB.Num() == HitsA.Num())
		for (int32 I = 0; I < HitsA.Num(); ++I)
			TestTrue(FString::Printf(TEXT("hit %d same"), I), HitsA[I].X == HitsB[I].X && HitsA[I].Y == HitsB[I].Y && HitsA[I].W == HitsB[I].W
				&& HitsA[I].H == HitsB[I].H && HitsA[I].Action == HitsB[I].Action && HitsA[I].FocusId == HitsB[I].FocusId);

	// スキン付きの版も同じ数を返す（2 つの入口が同じ記録を持つ）
	TArray<FKaedeNodeRect> Rects2;
	FKaedeUiSkin Skin; Skin.Theme = Theme;
	ComputeDrawList(Tree, 1280, 720, Measure, nullptr, Skin, nullptr, &Rects2);
	TestEqual(TEXT("skin overload rect count"), Rects2.Num(), Rects.Num());
	return true;
}
