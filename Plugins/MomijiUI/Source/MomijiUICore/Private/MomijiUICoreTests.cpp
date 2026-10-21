// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// MomijiUICoreTests.cpp — MomijiUICore（UE 非依存の核）の純関数を直接確かめる MomijiUI.Core.*。
//
// Slate もブラウザも使わない。入力と期待値の表で、補間（interpolate・formatNum）・条件式（evalCondExpr）・
// repeat の展開（templates / templateKey・instantiateTemplate）・テーマの読み込み（select・gradient・絵の参照）・
// 色の読み（parseHexColor の失敗）・型の違う JSON を既定値で読む経路（nodeFromJson）を見る。
// 配置・描画の契約は MomijiUISlate の MomijiUI.*（MomijiUISlateTests.cpp）、動きは MomijiUI.Core.Motion.*（KaedeUiMotionTests.cpp）。
// 2026-10-18（Issue #76・レビュー R3-MOMIJI-14）。
#include "Misc/AutomationTest.h"

#include "core/UiNode.hpp"
#include "ui/Interp.hpp"
#include <nlohmann/json.hpp>

using namespace kaedevn::ui;

namespace
{
	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	// 文字列の表で引く VarEnv（未知のキーは false＝補間はリテラルのまま）
	VarEnv StrEnv(const std::unordered_map<std::string, std::string>& Strs)
	{
		VarEnv Env;
		Env.resolveStr = [Strs](const std::string& Key, std::string& Out)
		{
			const auto It = Strs.find(Key);
			if (It == Strs.end()) return false;
			Out = It->second;
			return true;
		};
		return Env;
	}

	// 固定の一覧を返す RepeatProvider
	RepeatProvider ListProvider(const std::string& Name, const std::vector<ItemScope>& Items)
	{
		RepeatProvider P;
		P.getList = [Name, Items](const std::string& ListName, std::vector<ItemScope>& Out)
		{
			if (ListName != Name) return false;
			Out = Items;
			return true;
		};
		return P;
	}

	bool SameColor(const Color& C, uint8_t R, uint8_t G, uint8_t B, uint8_t A) { return C.r == R && C.g == G && C.b == B && C.a == A; }
}

// ===== formatNum: 整数は小数点なし・それ以外は最短表記（"{var}" の数値の見た目）=====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreFormatNumTest, "MomijiUI.Core.FormatNum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreFormatNumTest::RunTest(const FString& Parameters)
{
	struct FRow { double V; const TCHAR* Expected; };
	const FRow Rows[] = {
		{ 0.0,        TEXT("0") },
		{ 3.0,        TEXT("3") },
		{ -2.0,       TEXT("-2") },
		{ 1234567.0,  TEXT("1234567") },
		{ 2.5,        TEXT("2.5") },
		{ -0.25,      TEXT("-0.25") },
	};
	for (const FRow& R : Rows)
		TestEqual(*FString::Printf(TEXT("formatNum(%g)"), R.V), U(formatNum(R.V)), FString(R.Expected));
	return true;
}

// ===== interpolate: 既知のキーだけ置き換え、未知のキー・閉じ括弧の無い "{" はそのまま =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreInterpolateTest, "MomijiUI.Core.Interpolate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreInterpolateTest::RunTest(const FString& Parameters)
{
	const VarEnv Env = StrEnv({ {"name", "Alma"}, {"gold", "120"}, {"item.id", "herb"} });
	struct FRow { const char* In; const TCHAR* Expected; const TCHAR* What; };
	const FRow Rows[] = {
		{ "plain",               TEXT("plain"),               TEXT("{ が無ければそのまま") },
		{ "{name}",              TEXT("Alma"),                TEXT("既知のキー") },
		{ "{name} has {gold}G",  TEXT("Alma has 120G"),       TEXT("複数のキー") },
		{ "{unknown}",           TEXT("{unknown}"),           TEXT("未知のキーはリテラルのまま") },
		{ "{name}{unknown}",     TEXT("Alma{unknown}"),       TEXT("既知と未知が並ぶ") },
		{ "open {name",          TEXT("open {name"),          TEXT("閉じ括弧が無い") },
		{ "{item.id}",           TEXT("herb"),                TEXT("点を含むキー") },
		{ "{}",                  TEXT("{}"),                  TEXT("空のキーは未知") },
	};
	for (const FRow& R : Rows)
		TestEqual(R.What, U(interpolate(R.In, Env)), FString(R.Expected));

	// resolveStr が無ければ何も置き換えない
	TestEqual(TEXT("resolveStr 無し"), U(interpolate("{name}", VarEnv{})), FString(TEXT("{name}")));
	return true;
}

// ===== evalCondExpr: 項（名前・比較・数値リテラル）と合成（! && || 括弧）=====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreCondExprTest, "MomijiUI.Core.CondExpr",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreCondExprTest::RunTest(const FString& Parameters)
{
	const std::unordered_map<std::string, double> Vars = { {"a", 1.0}, {"b", 0.0}, {"hp", 30.0}, {"self.opened", 2.0} };
	VarEnv Env;
	Env.resolveNum = numResolver(Vars);
	struct FRow { const char* Expr; bool Expected; };
	const FRow Rows[] = {
		{ "a",                    true  },   // 非 0
		{ "b",                    false },
		{ "missing",              false },   // 未設定は 0
		{ "hp >= 30",             true  },
		{ "hp > 30",              false },
		{ "hp != 30",             false },   // != は否定ではなく比較
		{ "self.opened == 2",     true  },   // 名前に点
		{ "1",                    true  },   // 数値リテラル（repeat で補間済みの "{item.can}"）
		{ "0",                    false },
		{ "-2.5",                 true  },
		{ "!b",                   true  },
		{ "a && b",               false },
		{ "a || b",               true  },
		{ "!(a && b)",            true  },
		{ "(b || a) && hp >= 30", true  },
		{ "(a",                   true  },   // 閉じ括弧の欠落は寛容
		{ "()",                   false },   // 空の項は偽
		{ "a &&",                 false },   // 右辺の欠落は偽
	};
	for (const FRow& R : Rows)
		TestTrue(*U(R.Expr), evalCondExpr(R.Expr, Env) == R.Expected);
	return true;
}

// ===== expandRepeats: templateKey で型を選ぶ・無ければ template・どちらも無い要素は出さない =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreExpandRepeatsTest, "MomijiUI.Core.ExpandRepeats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreExpandRepeatsTest::RunTest(const FString& Parameters)
{
	const std::vector<ItemScope> Items = {
		{ {"kind", "say"},  {"n", "1"} },
		{ {"kind", "pick"}, {"n", "2"} },
		{ {"kind", "zzz"},  {"n", "3"} },   // templates に無い種類
		{ {"n", "4"} },                     // templateKey の値が無い
	};

	// template あり: 合わない要素は template で出る
	{
		UiNode N = nodeFromJson(nlohmann::json::parse(R"({ "type":"VerticalLayout","repeat":"log","templateKey":"kind",
			"template":  {"type":"Label","text":"T{item.n}"},
			"templates": { "say":  {"type":"Label","text":"S{item.n}"},
			               "pick": {"type":"Button","text":"P","action":"pick:{item.n}"} } })"));
		expandRepeats(N, ListProvider("log", Items));
		TestTrue(TEXT("展開したら repeat は解除"), N.repeat.empty());
		if (!TestEqual(TEXT("4 行"), (int32)N.children.size(), 4)) return false;
		TestEqual(TEXT("say の型"), U(N.children[0].text), FString(TEXT("S1")));
		TestTrue(TEXT("pick の型は Button"), N.children[1].type == UiType::Button);
		TestEqual(TEXT("pick の action を補間"), U(N.children[1].action), FString(TEXT("pick:2")));
		TestEqual(TEXT("知らない種類は template"), U(N.children[2].text), FString(TEXT("T3")));
		TestEqual(TEXT("種類の無い要素は template"), U(N.children[3].text), FString(TEXT("T4")));
		bool bAllFromRepeat = true;
		for (const UiNode& C : N.children) bAllFromRepeat &= C.fromRepeat;
		TestTrue(TEXT("複製は fromRepeat"), bAllFromRepeat);
	}
	// template 無し: 合う型の無い要素は出さない
	{
		UiNode N = nodeFromJson(nlohmann::json::parse(R"({ "type":"VerticalLayout","repeat":"log","templateKey":"kind",
			"templates": { "say": {"type":"Label","text":"S{item.n}"} } })"));
		expandRepeats(N, ListProvider("log", Items));
		if (TestEqual(TEXT("say の 1 行だけ"), (int32)N.children.size(), 1))
			TestEqual(TEXT("say の行"), U(N.children[0].text), FString(TEXT("S1")));
	}
	// 一覧が無い: 子は触らず repeat だけ解除
	{
		UiNode N = nodeFromJson(nlohmann::json::parse(R"({ "type":"VerticalLayout","repeat":"other",
			"template":{"type":"Label"}, "children":[{"type":"Label","text":"keep"}] })"));
		expandRepeats(N, ListProvider("log", Items));
		TestTrue(TEXT("一覧が無くても repeat は解除"), N.repeat.empty());
		if (TestEqual(TEXT("子はそのまま"), (int32)N.children.size(), 1))
			TestEqual(TEXT("子の中身もそのまま"), U(N.children[0].text), FString(TEXT("keep")));
	}
	return true;
}

// ===== instantiateTemplate: 1 行ぶんの {item.<key>} を text・action・id・sprite・条件へ。入れ子の repeat は展開しない =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreInstantiateTemplateTest, "MomijiUI.Core.InstantiateTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreInstantiateTemplateTest::RunTest(const FString& Parameters)
{
	const UiNode Tpl = nodeFromJson(nlohmann::json::parse(R"({ "type":"Button","id":"row-{item.id}","text":"{item.name}",
		"action":"pick:{item.id}","sprite":"icons/{item.icon}.png","enabledIf":"{item.can}","showIf":"{item.show}",
		"children":[ {"type":"Label","text":"{item.name} {item.zzz} {gold}"},
		             {"type":"VerticalLayout","repeat":"inner","template":{"type":"Label","text":"{item.name}"}} ] })"));
	const ItemScope Scope = { {"id", "herb"}, {"name", "薬草"}, {"icon", "leaf"}, {"can", "1"}, {"show", "0"} };
	const UiNode Row = instantiateTemplate(Tpl, Scope);

	TestTrue(TEXT("fromRepeat"), Row.fromRepeat);
	TestEqual(TEXT("id"), U(Row.id), FString(TEXT("row-herb")));
	TestEqual(TEXT("text"), U(Row.text), FString(TEXT("薬草")));
	TestEqual(TEXT("action"), U(Row.action), FString(TEXT("pick:herb")));
	TestEqual(TEXT("sprite"), U(Row.sprite), FString(TEXT("icons/leaf.png")));
	TestEqual(TEXT("enabledIf"), U(Row.enabledIf), FString(TEXT("1")));
	TestEqual(TEXT("showIf"), U(Row.showIf), FString(TEXT("0")));
	if (!TestEqual(TEXT("子は 2 つ"), (int32)Row.children.size(), 2)) return false;
	TestEqual(TEXT("子にも効く・未知の item と item 以外はそのまま"), U(Row.children[0].text), FString(TEXT("薬草 {item.zzz} {gold}")));
	TestEqual(TEXT("入れ子の repeat は展開しない"), U(Row.children[1].repeat), FString(TEXT("inner")));
	TestEqual(TEXT("元の template は変わらない"), U(Tpl.text), FString(TEXT("{item.name}")));
	return true;
}

// ===== themeFromJson: 選んだ行（select）・窓の濃淡（gradient のオブジェクト）・絵の参照・読めない値は base のまま =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreThemeFromJsonTest, "MomijiUI.Core.ThemeFromJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreThemeFromJsonTest::RunTest(const FString& Parameters)
{
	const UiTheme Base;
	{
		const UiTheme T = themeFromJson(nlohmann::json::parse(R"({
			"accent":"#zzzzzz", "panel":"#102030", "border":0, "knockout":true,
			"gradient":{"dir":"v","stops":[["#ff0000",0],["#0000ff",1.5]]},
			"frameImage":{"sprite":"frame.png","slice":[8,8,8,8],"opacity":0.5},
			"pattern":{"sprite":"dots.png","slice":[1,2,3]},
			"select":{"fill":"#336699","text":"#ffff00","cursor":{"sprite":"hand.png","w":30,"h":20,"dx":-34,"dy":2},
			          "gradient":{"dir":"h","stops":[["#000000",0],["#ffffff",1]]}} })"), Base);
		TestTrue(TEXT("読めない色は base のまま"), SameColor(T.accent, Base.accent.r, Base.accent.g, Base.accent.b, Base.accent.a));
		TestTrue(TEXT("panel"), SameColor(T.panel, 0x10, 0x20, 0x30, 0xff));
		TestEqual(TEXT("border は 1 以上"), T.border, 1);
		TestTrue(TEXT("knockout"), T.knockout);
		// 窓の濃淡（オブジェクト）: 2 色・縦・位置は 0〜1 に収める
		TestTrue(TEXT("windowGradient on"), T.windowGradient.on);
		TestFalse(TEXT("windowGradient 縦"), T.windowGradient.horizontal);
		if (TestEqual(TEXT("windowGradient 2 色"), (int32)T.windowGradient.stops.size(), 2))
			TestEqual(TEXT("位置は 1 に収まる"), T.windowGradient.stops[1].second, 1.f);
		TestEqual(TEXT("gradient がオブジェクトなら色の gradient は触らない"), (int32)T.gradient.a, 0);
		// 絵の参照
		TestEqual(TEXT("frameImage sprite"), U(T.frameImage.sprite), FString(TEXT("frame.png")));
		TestEqual(TEXT("frameImage slice"), T.frameImage.slice[2], 8);
		TestEqual(TEXT("frameImage opacity"), T.frameImage.opacity, 0.5f);
		TestEqual(TEXT("slice の数が違えば 0 のまま"), T.pattern.slice[0], 0);
		// 選んだ行
		TestTrue(TEXT("select on"), T.select.on);
		TestTrue(TEXT("select fill"), T.select.hasFill && SameColor(T.select.fill, 0x33, 0x66, 0x99, 0xff));
		TestTrue(TEXT("select text"), T.select.hasText && SameColor(T.select.text, 0xff, 0xff, 0x00, 0xff));
		TestFalse(TEXT("select border は書いていない"), T.select.hasBorder);
		TestEqual(TEXT("select cursor sprite"), U(T.select.cursor.sprite), FString(TEXT("hand.png")));
		TestTrue(TEXT("select cursor の寸法"), T.select.cursor.w == 30 && T.select.cursor.h == 20 && T.select.cursor.dx == -34 && T.select.cursor.dy == 2);
		TestTrue(TEXT("select gradient 横"), T.select.gradient.on && T.select.gradient.horizontal);
	}
	// 濃淡の色が 1 つしか読めなければ濃淡にしない。gradient が文字列なら窓の地へ重ねる色
	{
		const UiTheme T = themeFromJson(nlohmann::json::parse(R"({
			"gradient":"#00000080", "select":{"gradient":{"stops":[["#000000",0],["bad",1]]}} })"), Base);
		TestFalse(TEXT("文字列の gradient は windowGradient にしない"), T.windowGradient.on);
		TestTrue(TEXT("文字列の gradient は色"), SameColor(T.gradient, 0, 0, 0, 0x80));
		TestFalse(TEXT("読める色が 1 つの濃淡は off"), T.select.gradient.on);
	}
	return true;
}

// ===== 色の読み（parseHexColor）: "#rrggbb" と "#rrggbbaa" だけ。読めなければ色を付けない =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreHexColorTest, "MomijiUI.Core.HexColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreHexColorTest::RunTest(const FString& Parameters)
{
	auto Read = [](const nlohmann::json& Value, Color& Out)
	{
		const UiNode N = nodeFromJson(nlohmann::json{ {"type", "Panel"}, {"color", Value} });
		Out = N.color;
		return N.hasColor;
	};
	struct FRow { const char* In; bool bOk; uint8_t R, G, B, A; };
	const FRow Rows[] = {
		{ "#ff8000",   true,  0xff, 0x80, 0x00, 0xff },
		{ "#FF8000",   true,  0xff, 0x80, 0x00, 0xff },   // 大文字
		{ "#ff800080", true,  0xff, 0x80, 0x00, 0x80 },
		{ "ff8000",    false, 0, 0, 0, 0 },               // # が無い
		{ "#ff80",     false, 0, 0, 0, 0 },               // 長さ
		{ "#ff8000f",  false, 0, 0, 0, 0 },               // 8 字
		{ "#gg0000",   false, 0, 0, 0, 0 },               // 16 進でない
		{ "",          false, 0, 0, 0, 0 },
	};
	for (const FRow& R : Rows)
	{
		Color C{};
		const bool bOk = Read(R.In, C);
		TestTrue(*FString::Printf(TEXT("'%s' を読めるか"), *U(R.In)), bOk == R.bOk);
		if (R.bOk && bOk) TestTrue(*FString::Printf(TEXT("'%s' の色"), *U(R.In)), SameColor(C, R.R, R.G, R.B, R.A));
	}
	Color C{};
	TestFalse(TEXT("文字列でない色は読まない"), Read(123, C));
	return true;
}

// ===== nodeFromJson: 型の違う値は既定値で読み、例外を投げない（1 つの型違いで画面全体を空にしない）=====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMomijiUICoreNodeFromJsonTypesTest, "MomijiUI.Core.NodeFromJsonTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMomijiUICoreNodeFromJsonTypesTest::RunTest(const FString& Parameters)
{
	const nlohmann::json J = nlohmann::json::parse(R"({ "type":5, "x":"10", "w":true, "gap":"8", "text":3, "enabled":"no",
		"fontSize":[16], "dim":0, "template":"bad", "runs":[ "bad", {"text":"r","bold":"yes"} ],
		"points":[ [1,2], ["a",3], [4], 5 ],
		"children":[ {"type":"Label","text":"ok"}, 7 ] })");
	UiNode N;
	try { N = nodeFromJson(J); }
	catch (const std::exception& E) { AddError(FString::Printf(TEXT("例外: %s"), *U(E.what()))); return false; }

	TestTrue(TEXT("type が数なら Panel"), N.type == UiType::Panel);
	TestEqual(TEXT("x が文字列なら 0"), N.x, 0);
	TestEqual(TEXT("w が真偽なら 0"), N.w, 0);
	TestEqual(TEXT("gap が文字列なら既定 8"), N.gap, 8);
	TestEqual(TEXT("text が数なら空"), U(N.text), FString());
	TestTrue(TEXT("enabled が文字列なら既定 true"), N.enabled);
	TestEqual(TEXT("fontSize が配列なら 0"), N.fontPt, 0);
	TestFalse(TEXT("dim は数も受ける（0 は false）"), N.dim);
	TestFalse(TEXT("template がオブジェクトでなければ無し"), (bool)N.repeatTemplate);
	if (TestEqual(TEXT("runs はオブジェクトだけ"), (int32)N.runs.size(), 1))
		TestFalse(TEXT("run の bold が文字列なら false"), N.runs[0].bold);
	if (TestEqual(TEXT("points は [x, y] だけ"), (int32)N.points.size(), 1))
		TestTrue(TEXT("points[0]"), N.points[0].x == 1 && N.points[0].y == 2);
	if (TestEqual(TEXT("子は 2 つ（数の子は既定の Panel）"), (int32)N.children.size(), 2))
	{
		TestEqual(TEXT("子の text"), U(N.children[0].text), FString(TEXT("ok")));
		TestTrue(TEXT("数の子は Panel"), N.children[1].type == UiType::Panel);
		TestEqual(TEXT("子の srcPath"), U(N.children[1].srcPath), FString(TEXT("/children/1")));
	}
	return true;
}
