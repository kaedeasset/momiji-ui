// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiMotionTests.cpp — UIモーション（イージング / トゥイーン / anim スキーマ）の自動テスト。
//
// `MomijiUI.Core.*` として Automation に載る（2026-10-18 に Kaede.Ui.* から改名・Issue #76）。widget を作らず、update(deltaMs) を手で
// ステップして値を検証する＝GUI 無し・ヘッドレスで回る。
//
// イージング11種は monorepo の easing.ts（語彙の正）のテストベクトルと**数値一致**で
// 検証する。web 側が独自実装で二重化した経緯があるため、C++ は移植元と数値で揃える。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "Misc/AutomationTest.h"

#include "ui/Easing.hpp"
#include "ui/Tween.hpp"
#include "core/UiAnim.hpp"
#include <nlohmann/json.hpp>

// M0: イージング11種を monorepo packages/core/src/timeline/easing.ts（語彙の正）の
// テストベクトルと数値一致で検証する（easing.test.ts から移植。設計 07/21/01 §検証計画1）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeEasingTest, "MomijiUI.Core.Easing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeEasingTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;

	struct FVec { Easing E; double T; double Expected; };
	const FVec Vecs[] = {
		{ Easing::Linear,         0.0,   0.0 },
		{ Easing::Linear,         0.5,   0.5 },
		{ Easing::Linear,         1.0,   1.0 },
		{ Easing::EaseIn,         0.5,   0.25 },      // 0.5^2
		{ Easing::EaseOut,        0.5,   0.75 },      // 1-(1-0.5)^2
		{ Easing::EaseInOut,      0.25,  0.125 },     // 2*0.25^2
		{ Easing::EaseInOut,      0.5,   0.5 },
		{ Easing::EaseInOut,      0.75,  0.875 },
		{ Easing::EaseInQuad,     0.5,   0.25 },
		{ Easing::EaseOutQuad,    0.5,   0.75 },
		{ Easing::EaseInOutQuad,  0.5,   0.5 },
		{ Easing::EaseInCubic,    0.5,   0.125 },     // 0.5^3
		{ Easing::EaseOutCubic,   0.5,   0.875 },     // 1-(1-0.5)^3
		{ Easing::EaseInOutCubic, 0.25,  0.0625 },    // 4*0.25^3
		{ Easing::EaseInOutCubic, 0.5,   0.5 },
		{ Easing::Step,           0.0,   0.0 },
		{ Easing::Step,           0.5,   0.0 },
		{ Easing::Step,           0.999, 0.0 },
		{ Easing::Step,           1.0,   1.0 },
	};
	for (const FVec& V : Vecs)
	{
		TestNearlyEqual(FString::Printf(TEXT("easing %d at t=%f"), (int32)V.E, V.T),
			applyEasing(V.T, V.E), V.Expected, 1e-9);
	}

	// 端点は全種で 0→0 / 1→1（step 含む）。
	for (int32 i = 0; i <= (int32)Easing::Step; ++i)
	{
		TestNearlyEqual(TEXT("t=0 -> 0"), applyEasing(0.0, (Easing)i), 0.0, 1e-9);
		TestNearlyEqual(TEXT("t=1 -> 1"), applyEasing(1.0, (Easing)i), 1.0, 1e-9);
	}

	// 名前ディスパッチ（easing.ts applyEasing と同じ挙動・未知は linear フォールバック）。
	bool bOk = false;
	TestNearlyEqual(TEXT("byName easeIn"), applyEasing(0.5, easingFromString("easeIn", &bOk)), 0.25, 1e-9);
	TestTrue(TEXT("easeIn known"), bOk);
	TestNearlyEqual(TEXT("unknown -> linear"), applyEasing(0.5, easingFromString("bogus", &bOk)), 0.5, 1e-9);
	TestFalse(TEXT("bogus unknown"), bOk);

	// クランプ版。
	TestNearlyEqual(TEXT("safe clamp low"),  applyEasingSafe(-1.0, Easing::EaseIn), 0.0, 1e-9);
	TestNearlyEqual(TEXT("safe clamp high"), applyEasingSafe(2.0,  Easing::EaseIn), 1.0, 1e-9);

	return true;
}

// ===== M1: UiMotionState の決定論テスト =====
// deltaMs を手動ステップ供給し、enter の alpha/offset、stagger、countUp、pulse、endSync の破棄を widget 無しで検証する
//（設計 07/21/01 §検証計画2）。2026-10-18（Issue #76）に 9 題を 1 本に束ねていたのを題ごとに分けた（落ちたとき名前で機能が分かる）。
namespace
{
	// enter slide（bottom, distance=40, 200ms, linear）
	kaedevn::ui::UiAnimSpec SlideSpec()
	{
		kaedevn::ui::UiAnimSpec Slide;
		Slide.enterType = "slide";
		Slide.enterFrom = "bottom";
		Slide.enterDistance = 40.f;
		Slide.enterDurationMs = 200;
		Slide.enterEasing = "linear";
		return Slide;
	}
	// pulse（scale 1.10・周期 1000ms）
	kaedevn::ui::UiAnimSpec PulseSpec()
	{
		kaedevn::ui::UiAnimSpec Pulse;
		Pulse.hasPulse = true;
		Pulse.pulseScale = 1.10f;
		Pulse.pulsePeriodMs = 1000;
		return Pulse;
	}
}

// enter slide: 始めは alpha 0・offY=distance、半分で 0.5、終われば sample 無し。同じキーの作り直しでは再発火しない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionEnterTest, "MomijiUI.Core.Motion.Enter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionEnterTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Slide = SlideSpec();

	// キーは識別子ベース（<scope>/<id|action>。設計 07/21/04 §A1）。位置は一切含めない。
	UiMotionState M;
	M.beginSync("home");
	M.noteNode("home/card", "home", Slide, false, 0.0, false, false);
	M.endSync();

	UiMotionSample S;
	TestTrue(TEXT("enter active at t=0"), M.sample("home/card", S));
	TestNearlyEqual(TEXT("alpha 0 at start"), (double)S.alpha, 0.0, 1e-6);
	TestNearlyEqual(TEXT("offY = distance at start"), (double)S.offY, 40.0, 1e-4);

	M.update(100.0);   // 半分
	M.sample("home/card", S);
	TestNearlyEqual(TEXT("alpha 0.5 at mid"), (double)S.alpha, 0.5, 1e-4);
	TestNearlyEqual(TEXT("offY 20 at mid"), (double)S.offY, 20.0, 1e-3);

	M.update(150.0);   // 完了（250ms > 200ms）
	TestFalse(TEXT("enter finished -> no sample"), M.sample("home/card", S));
	TestEqual(TEXT("no active tracks after enter"), M.activeCount(), 0);

	// 再Rebuild（同キー継続）では enter は再発火しない。
	M.beginSync("home");
	M.noteNode("home/card", "home", Slide, false, 0.0, false, false);
	M.endSync();
	TestFalse(TEXT("no re-enter for persistent key"), M.sample("home/card", S));
	return true;
}

// stagger: 遅延順位は「この同期で新規になった同じグループ内での順位」（§B3）。
// 表示位置ではないので、行の並びではなく「新規になった順」で 0,1,2 の階段になる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionStaggerTest, "MomijiUI.Core.Motion.Stagger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionStaggerTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Slide = SlideSpec();
	UiMotionState M;
	UiMotionSample S;
	UiAnimSpec Stag = Slide;
	Stag.staggerMs = 50;
	M.beginSync("home2");
	M.noteNode("home2/card", "home2", Slide, false, 0.0, false, false);
	M.noteNode("home2/row-a", "home2/list", Stag, false, 0.0, false, false);
	M.noteNode("home2/row-b", "home2/list", Stag, false, 0.0, false, false);
	M.noteNode("home2/row-c", "home2/list", Stag, false, 0.0, false, false);
	M.endSync();
	M.update(50.0);
	M.sample("home2/row-c", S);
	TestNearlyEqual(TEXT("3rd new row still at start after 50ms"), (double)S.alpha, 0.0, 1e-6);
	M.sample("home2/row-a", S);
	TestNearlyEqual(TEXT("1st new row has no delay"), (double)S.alpha, 0.25, 1e-4);
	M.update(150.0);   // 経過200ms: delay100 + 100/200 = 進行0.5
	M.sample("home2/row-c", S);
	TestNearlyEqual(TEXT("3rd new row mid after delay"), (double)S.alpha, 0.5, 1e-4);
	M.update(300.0);   // 全部完了させる

	// 画面内で1行だけ増えたら、その行は「新規の1件目」＝遅延0で即スライドインする
	//（従来は表示位置ぶんの遅延が付き、10行目なら 400ms 待たされた）。
	M.beginSync("home2");
	M.noteNode("home2/card", "home2", Slide, false, 0.0, false, false);
	M.noteNode("home2/row-a", "home2/list", Stag, false, 0.0, false, false);
	M.noteNode("home2/row-b", "home2/list", Stag, false, 0.0, false, false);
	M.noteNode("home2/row-c", "home2/list", Stag, false, 0.0, false, false);
	M.noteNode("home2/row-d", "home2/list", Stag, false, 0.0, false, false);
	M.endSync();
	M.update(50.0);
	TestTrue(TEXT("added row enters"), M.sample("home2/row-d", S));
	TestNearlyEqual(TEXT("added row has no stagger delay"), (double)S.alpha, 0.25, 1e-4);
	TestFalse(TEXT("existing rows do not re-enter"), M.sample("home2/row-a", S));
	return true;
}

// 行の削除/並び替えでキーがズレない（設計 07/21/04 の主眼）。
// sel:a を消しても、残った sel:b の状態が sel:a のものに置き換わらない＝別ボタンが動かない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionRowKeysTest, "MomijiUI.Core.Motion.RowKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionRowKeysTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Slide = SlideSpec();
	UiMotionState M;
	UiMotionSample S;
	M.beginSync("list");
	M.noteNode("list/sel:a", "list/rows", Slide, false, 0.0, false, false);
	M.noteNode("list/sel:b", "list/rows", Slide, false, 0.0, false, false);
	M.endSync();
	M.update(500.0);   // 両方の enter を終わらせる
	M.beginSync("list");
	M.noteNode("list/sel:b", "list/rows", Slide, false, 0.0, false, false);   // a が消え b が先頭へ
	M.endSync();
	TestFalse(TEXT("row removal does not re-enter the survivor"), M.sample("list/sel:b", S));
	// 消えた行が戻ってきたら、その行だけが enter する（画面内でも発火する＝bFreshScreen 廃止）。
	M.beginSync("list");
	M.noteNode("list/sel:a", "list/rows", Slide, false, 0.0, false, false);
	M.noteNode("list/sel:b", "list/rows", Slide, false, 0.0, false, false);
	M.endSync();
	TestTrue(TEXT("re-appeared row enters"), M.sample("list/sel:a", S));
	TestFalse(TEXT("untouched row stays still"), M.sample("list/sel:b", S));
	return true;
}

// 画面が変われば新しい scope のキー＝全ノード enter
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionScreenScopeTest, "MomijiUI.Core.Motion.ScreenScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionScreenScopeTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Slide = SlideSpec();
	UiMotionState M;
	UiMotionSample S;
	M.beginSync("home");
	M.noteNode("home/card", "home", Slide, false, 0.0, false, false);
	M.endSync();
	M.update(500.0);   // home の enter を終わらせる
	M.beginSync("craft");
	M.noteNode("craft/card", "craft", Slide, false, 0.0, false, false);
	M.endSync();
	TestTrue(TEXT("enter re-fires after screen change"), M.sample("craft/card", S));
	TestNearlyEqual(TEXT("fresh enter alpha 0"), (double)S.alpha, 0.0, 1e-6);
	return true;
}

// countUp: 1000→2000, 400ms linear。初回は即時・変化で補間・整数丸め＋桁区切り
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionCountUpTest, "MomijiUI.Core.Motion.CountUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionCountUpTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	UiMotionSample S;
	UiAnimSpec Count;
	Count.hasCountUp = true;
	Count.countUpDurationMs = 400;
	Count.countUpEasing = "linear";

	// HUD の scope は "hud" 固定（§A1/E1）。画面（タブ）を切り替えても前回値が持続し、
	// gold の増分が countUp で回る＝タブ切替のたびに状態が捨てられない。
	UiMotionState M2;
	M2.beginSync("home");
	M2.noteNode("hud/hudGold", "hud", Count, true, 1000.0, true, false);
	M2.endSync();
	TestFalse(TEXT("countUp: first bind is immediate"), M2.sample("hud/hudGold", S));

	M2.beginSync("craft");   // 画面切替を挟む
	M2.noteNode("hud/hudGold", "hud", Count, true, 2000.0, true, false);
	M2.endSync();
	M2.update(200.0);   // 半分 → 1500
	TestTrue(TEXT("countUp active across screen change"), M2.sample("hud/hudGold", S));
	TestTrue(TEXT("countUp overrides text"), S.hasText);
	TestEqual(TEXT("countUp mid value grouped"), FString(S.text.c_str()), FString(TEXT("1,500")));
	M2.update(300.0);   // 完了
	TestFalse(TEXT("countUp finished"), M2.sample("hud/hudGold", S));
	return true;
}

// pulse: whileIf 真の間だけ。周期の中点で最大スケール・作動開始は 1.0。HUD のキーは画面切替を跨いで位相が続く
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionPulseTest, "MomijiUI.Core.Motion.Pulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionPulseTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Pulse = PulseSpec();
	UiMotionSample S;
	UiMotionState M3;
	M3.beginSync("home");
	M3.noteNode("hud/badgeGather", "hud", Pulse, false, 0.0, false, true);
	M3.endSync();
	TestTrue(TEXT("pulse active"), M3.sample("hud/badgeGather", S));
	TestNearlyEqual(TEXT("pulse starts at 1.0"), (double)S.scale, 1.0, 1e-4);
	M3.update(500.0);   // 周期中点
	M3.sample("hud/badgeGather", S);
	TestNearlyEqual(TEXT("pulse peak at half period"), (double)S.scale, 1.10, 1e-3);
	// 画面（タブ）を切り替えても scope 固定の HUD キーは生き残り、位相が続く。
	M3.beginSync("craft");
	M3.noteNode("hud/badgeGather", "hud", Pulse, false, 0.0, false, true);
	M3.endSync();
	M3.sample("hud/badgeGather", S);
	TestNearlyEqual(TEXT("pulse phase survives screen change"), (double)S.scale, 1.10, 1e-3);
	M3.update(200.0);   // 画面切替で始まった画面遷移エンター(160ms)を終わらせる
	// 条件が偽になったら止まる。
	M3.beginSync("craft");
	M3.noteNode("hud/badgeGather", "hud", Pulse, false, 0.0, false, false);
	M3.endSync();
	TestFalse(TEXT("pulse stops when condition false"), M3.sample("hud/badgeGather", S));
	TestEqual(TEXT("no active tracks when idle"), M3.activeCount(), 0);
	return true;
}

// キー検証（§A2）: 正常データはログ0件・同一同期のキー重複はエラー
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionKeyCheckTest, "MomijiUI.Core.Motion.KeyCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionKeyCheckTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	const UiAnimSpec Pulse = PulseSpec();
	{
		UiMotionState M5;
		M5.beginSync("craft");
		M5.noteNode("craft/sel:a", "craft/rows", Pulse, false, 0.0, false, false);
		M5.noteNode("craft/sel:b", "craft/rows", Pulse, false, 0.0, false, false);
		M5.endSync();
		int32 Errors = 0;
		for (const std::string& L : M5.drainLogs()) if (L.rfind("ERROR ", 0) == 0) ++Errors;
		TestEqual(TEXT("no validation errors for well-formed keys"), Errors, 0);

		M5.beginSync("craft");
		M5.noteNode("craft/sel:a", "craft/rows", Pulse, false, 0.0, false, false);
		M5.noteNode("craft/sel:a", "craft/rows", Pulse, false, 0.0, false, false);   // 重複
		M5.endSync();
		bool bDup = false;
		for (const std::string& L : M5.drainLogs())
			if (L.find("key-dup") != std::string::npos) bDup = true;
		TestTrue(TEXT("duplicate key is reported"), bDup);
	}
	return true;
}

// 画面遷移エンター（B3・07/21/02）: 画面切替で自動開始・160msで完了
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionScreenEnterTest, "MomijiUI.Core.Motion.ScreenEnter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionScreenEnterTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	{
		UiMotionState M4;
		UiMotionSample SE;
		M4.beginSync("home");
		M4.endSync();
		TestTrue(TEXT("screen enter starts on first screen"), M4.screenEnterSample(SE));
		TestNearlyEqual(TEXT("screen enter alpha 0 at start"), (double)SE.alpha, 0.0, 1e-6);
		TestNearlyEqual(TEXT("screen enter offY 10 at start"), (double)SE.offY, 10.0, 1e-4);
		M4.update(80.0);   // 半分（easeOutCubic(0.5)=0.875）
		M4.screenEnterSample(SE);
		TestNearlyEqual(TEXT("screen enter mid alpha"), (double)SE.alpha, 0.875, 1e-3);
		M4.update(100.0);  // 完了（180ms > 160ms）
		TestFalse(TEXT("screen enter done"), M4.screenEnterSample(SE));
		TestEqual(TEXT("idle after screen enter"), M4.activeCount(), 0);
		// 同じ画面の再Rebuildでは再発火しない。
		M4.beginSync("home");
		M4.endSync();
		TestFalse(TEXT("no re-fire on same screen"), M4.screenEnterSample(SE));
		// 画面が変われば再発火。
		M4.beginSync("craft");
		M4.endSync();
		TestTrue(TEXT("re-fire on screen change"), M4.screenEnterSample(SE));
	}
	return true;
}

// anim の JSON を読む（enter・stagger・countUp・pulse）。空のオブジェクトは nullptr
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionAnimJsonTest, "MomijiUI.Core.Motion.AnimJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionAnimJsonTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	auto Spec = animFromJson(nlohmann::json::parse(R"({
		"enter": { "type": "pop", "durationMs": 300, "easing": "easeOutCubic" },
		"staggerMs": 40,
		"countUp": { "durationMs": 500 },
		"pulse": { "scale": 1.06, "periodMs": 900, "whileIf": "slotReady" }
	})"));
	TestTrue(TEXT("anim parsed"), Spec != nullptr);
	if (Spec)
	{
		TestEqual(TEXT("enter type"), FString(Spec->enterType.c_str()), FString(TEXT("pop")));
		TestEqual(TEXT("enter duration"), Spec->enterDurationMs, 300);
		TestEqual(TEXT("stagger"), Spec->staggerMs, 40);
		TestTrue(TEXT("countUp on"), Spec->hasCountUp);
		TestTrue(TEXT("pulse on"), Spec->hasPulse);
		TestEqual(TEXT("pulse whileIf"), FString(Spec->pulseWhileIf.c_str()), FString(TEXT("slotReady")));
	}
	TestTrue(TEXT("empty anim -> nullptr"), animFromJson(nlohmann::json::object()) == nullptr);

	return true;
}

// ===== 2026-10-12 の動き（blink・bob・frames・open・exit・tween。rpg-themes/README.md §3.3）=====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKaedeUiMotionThemeTest, "MomijiUI.Core.MotionTheme",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKaedeUiMotionThemeTest::RunTest(const FString& Parameters)
{
	using namespace kaedevn::ui;
	auto Spec = [](const char* Json) { return animFromJson(nlohmann::json::parse(Json)); };
	UiMotionState M;
	auto Sync = [&](const std::vector<std::pair<std::string, std::shared_ptr<UiAnimSpec>>>& Nodes)
	{
		M.beginSync("s");
		for (const auto& N : Nodes) M.noteNode(N.first, "", *N.second, false, 0.0, false, false);
		M.endSync();
	};
	const auto Blink = Spec(R"({"blink":{"periodMs":1000,"min":0.2}})");
	const auto Bob = Spec(R"({"bob":{"periodMs":1000,"distance":5}})");
	const auto Frames = Spec(R"({"frames":{"count":4,"periodMs":100}})");
	const auto Open = Spec(R"({"enter":{"type":"open","durationMs":200}})");
	const auto Exit = Spec(R"({"exit":{"type":"close","durationMs":200}})");
	const auto Tween = Spec(R"({"tween":{"durationMs":200}})");
	TestTrue(TEXT("6 つの spec が読める"), Blink && Bob && Frames && Open && Exit && Tween);
	if (!(Blink && Bob && Frames && Open && Exit && Tween)) return false;

	Sync({{"s/blink", Blink}, {"s/bob", Bob}, {"s/frames", Frames}, {"s/open", Open}, {"s/exit", Exit}, {"s/tween", Tween}});
	UiMotionSample S;
	M.update(500.0);   // blink の半周＝いちばん薄い
	TestTrue(TEXT("blink: 半周で min"), M.sample("s/blink", S) && FMath::IsNearlyEqual(S.alpha, 0.2f, 0.02f));
	M.update(250.0);   // bob の 3/4 周＝上へ distance（sin = -1）
	TestTrue(TEXT("bob: 3/4 周で -distance"), M.sample("s/bob", S) && FMath::IsNearlyEqual(S.offY, -5.f, 0.1f));
	TestTrue(TEXT("frames: 750ms で 7 コマ目 % 4 = 3"), M.sample("s/frames", S) && S.frame == 3);
	TestFalse(TEXT("open: 終わったら sample は無し"), M.sample("s/open", S));

	// open の途中
	UiMotionState M2;
	M2.beginSync("s"); M2.noteNode("s/open", "", *Open, false, 0.0, false, false); M2.endSync();
	M2.update(100.0);
	TestTrue(TEXT("open: 途中は scaleY が 0〜1"), M2.sample("s/open", S) && S.scaleY > 0.f && S.scaleY < 1.f);

	// exit: 見えなくなったら閉じていく → 時間が経つと消える
	Sync({{"s/blink", Blink}});
	TestEqual(TEXT("exit: 消えたキーは exiting"), (int32)M.exitingKeys().size(), 1);
	M.update(100.0);
	TestTrue(TEXT("exit close: 途中は scaleY < 1"), M.sample("s/exit", S) && S.scaleY < 1.f && S.scaleY > 0.f);
	M.update(150.0);
	TestFalse(TEXT("exit: 終わったら消える"), M.sample("s/exit", S));
	TestEqual(TEXT("exit: exiting は空"), (int32)M.exitingKeys().size(), 0);

	// tween: 値が 0.5 → 1.0 に変わったら、棒は前の値から伸びる
	UiMotionState M3;
	M3.beginSync("s"); M3.noteNode("s/t", "", *Tween, false, 0.0, false, false); M3.noteValue("s/t", 0.5); M3.endSync();
	M3.beginSync("s"); M3.noteNode("s/t", "", *Tween, false, 0.0, false, false); M3.noteValue("s/t", 1.0); M3.endSync();
	TestTrue(TEXT("tween: 変わった直後は 0.5 / 1.0"), M3.sample("s/t", S) && FMath::IsNearlyEqual(S.valueScale, 0.5f, 0.01f));
	M3.update(300.0);
	TestFalse(TEXT("tween: 終わったら sample は無し"), M3.sample("s/t", S));
	return true;
}
