// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#include "Modules/ModuleManager.h"
#include "MomijiUICore.h"

// MomijiUI の純 C++ Core(nlohmann を引く)。例外有効・Unity 無効・IWYU None でこの .cpp 内に閉じる。
// 画面 JSON の解析・寸法の一括スケール・テーマ・prune／enabled・bind・診断・リッチテキストの配置を、
// 別モジュール（MomijiUI の描画器・KaedeVN・KaedeRPG・ADV）へ MOMIJIUICORE_API で公開する（Core ヘッダは無改変）。
// 2026-10-14 分割 S1 で KaedeCore.cpp から分けた（設計 Docs/Guides/Working/product-split-design.md §5.1）。
#include "core/UiNode.hpp"
#include "ui/Interp.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>  // std::max（ScaleUiTree の線幅の下限）
#include <cmath>    // std::lround（論理解像度スケールの丸め）
#include <memory>   // std::make_shared（ScaleUiTree の repeatTemplate 複製）
#include <string>

IMPLEMENT_MODULE(FDefaultModuleImpl, MomijiUICore);

DEFINE_LOG_CATEGORY_STATIC(LogMomijiUICore, Log, All);

namespace KaedeCore
{
	kaedevn::ui::UiNode ScreenTreeFromJson(const nlohmann::json& J)
	{
		// 静的画面はそのまま解析（{var} 補間や repeat は持たない想定）。
		// 裸ノード形式（quest.json 等）と、メタ付きラッパ形式
		// {"id","note","root":{...}}（Game/ui/config.json 等）の両方を受ける。
		// 鍵の型違いは nodeFromJson が既定値に落とす（例外を投げない）。
		if (J.is_object() && J.contains("root") && J["root"].is_object())
			return kaedevn::ui::nodeFromJson(J["root"], "/root");   // srcPath は JSON Pointer（包みの root から）
		return kaedevn::ui::nodeFromJson(J);
	}

	kaedevn::ui::UiNode LoadScreenTree(const std::string& Json, bool* bOutOk)
	{
		// JSON として読めなければログを出して空ノード（例外を UI 層へ漏らさない）。成否は bOutOk へ
		if (bOutOk) *bOutOk = false;
		try
		{
			kaedevn::ui::UiNode Tree = ScreenTreeFromJson(nlohmann::json::parse(Json));
			if (bOutOk) *bOutOk = true;
			return Tree;
		}
		catch (const nlohmann::json::exception& E)
		{
			UE_LOG(LogMomijiUICore, Error, TEXT("[momiji-ui] 画面 JSON が読めない（空の画面にする）: %s"), UTF8_TO_TCHAR(E.what()));
			return kaedevn::ui::UiNode{};
		}
	}

	void ScaleUiTree(kaedevn::ui::UiNode& Node, double Kx, double Ky)
	{
		if (Kx == 1.0 && Ky == 1.0) return;   // 等倍は完全な no-op（720p 運用は 1bit も変わらない）

		const double Kf = Kx < Ky ? Kx : Ky;  // gap/pad/margin/fontPt は等方スケール
		auto RX = [Kx](int V) { return (int)std::lround(V * Kx); };
		auto RY = [Ky](int V) { return (int)std::lround(V * Ky); };
		auto RF = [Kf](int V) { return (int)std::lround(V * Kf); };

		Node.x = RX(Node.x);  Node.w = RX(Node.w);  Node.minW  = RX(Node.minW);
		Node.y = RY(Node.y);  Node.h = RY(Node.h);  Node.minH  = RY(Node.minH);
		Node.wrapW = RX(Node.wrapW);
		Node.lineH = RY(Node.lineH);     // 行送りは縦方向。0（=ホスト既定）は 0 のまま
		Node.gap = RF(Node.gap);  Node.pad = RF(Node.pad);  Node.margin = RF(Node.margin);
		Node.fontPt = RF(Node.fontPt);   // 0（=ホスト既定）は 0 のまま

		// Polyline / Ellipse の幾何。**x/y と同じ空間にあるので一緒に動かさないと図が崩れる**——
		// 札（x/y）だけ 1920 へ動いて線（points）が 1280 に取り残される。
		// 2026-08-29 まで抜けていたが表面化しなかったのは、**それまでの唯一の呼び出し元
		// （SKaedeMomijiUIWidget:555 の VN）の JSON に Polyline/Ellipse が 1 つも無かった**ため。
		// Polyline を使うのは ADV の事件の図と攻略地図だけ（Game/adv*/ui/）。
		for (auto& P : Node.points) { P.x = RX(P.x); P.y = RY(P.y); }
		// 線幅は等方（Kf）。太さが 0 になると線が消えるので下限 1 で止める。
		if (Node.thickness > 0.0f)
			Node.thickness = (float)std::max(1.0, (double)Node.thickness * Kf);

		// repeatTemplate は shared_ptr で pristine ツリーと共有され得るので、
		// その場で書き換えず複製してからスケールする（毎フレーム倍率が累乗するのを防ぐ）。
		if (Node.repeatTemplate)
		{
			auto Cloned = std::make_shared<kaedevn::ui::UiNode>(*Node.repeatTemplate);
			ScaleUiTree(*Cloned, Kx, Ky);
			Node.repeatTemplate = Cloned;
		}
		for (auto& Kv : Node.repeatTemplates)   // 種類ごとの型（F2）も同じく複製してから
		{
			if (!Kv.second) continue;
			auto Cloned = std::make_shared<kaedevn::ui::UiNode>(*Kv.second);
			ScaleUiTree(*Cloned, Kx, Ky);
			Kv.second = Cloned;
		}
		for (auto& Child : Node.children) ScaleUiTree(Child, Kx, Ky);
	}

	kaedevn::ui::UiTheme LoadTheme(const std::string& Json, bool* bOutOk)
	{
		using namespace kaedevn::ui;
		if (bOutOk) *bOutOk = false;
		try
		{
			UiTheme T = themeFromJson(nlohmann::json::parse(Json), UiTheme{});
			if (bOutOk) *bOutOk = true;
			return T;
		}
		catch (const nlohmann::json::exception& E)
		{
			UE_LOG(LogMomijiUICore, Error, TEXT("[momiji-ui] テーマ JSON が読めない（既定のテーマにする）: %s"), UTF8_TO_TCHAR(E.what()));
			return UiTheme{};
		}
	}

	kaedevn::ui::UiTheme BuiltinTheme(const std::string& Name)
	{
		kaedevn::ui::UiTheme T;
		kaedevn::ui::builtinTheme(Name, T);  // 未知名は false → 既定(atelier)のまま
		return T;
	}

	void PruneScreenTree(kaedevn::ui::UiNode& Root,
	                     const std::unordered_map<std::string, double>& Vars)
	{
		kaedevn::ui::VarEnv Env;
		Env.resolveNum = kaedevn::ui::numResolver(Vars);
		kaedevn::ui::prune(Root, Env);
	}

	void ApplyEnabledTree(kaedevn::ui::UiNode& Root,
	                      const std::unordered_map<std::string, double>& Vars)
	{
		// enabledIf / highlightIf を評価する。**PruneScreenTree とは別関数にしてある**——
		// あちらは prune だけで、MomijiUI の本編とプレビューが呼んでいる。
		// そちらの見た目を変えずに ADV だけが選択枠を得られるようにするため、足す側で分ける
		//（2026-08-29。ADV の外殻は highlightIf を一度も評価していなかった）。
		kaedevn::ui::VarEnv Env;
		Env.resolveNum = kaedevn::ui::numResolver(Vars);
		kaedevn::ui::applyEnabled(Root, Env);
	}

	std::vector<kaedevn::ui::UiDiagnostic> ValidateScreenJson(const std::string& Json)
	{
		const nlohmann::json J = nlohmann::json::parse(Json, nullptr, false);
		if (J.is_discarded()) return { { "error", "bad-value", "", "JSON として読めない" } };
		return kaedevn::ui::validateScreen(J);
	}

	kaedevn::ui::UiNode InstantiateTemplate(const kaedevn::ui::UiNode& Tpl, const kaedevn::ui::ItemScope& Scope)
	{
		return kaedevn::ui::instantiateTemplate(Tpl, Scope);
	}

	void BindScreenTree(
		kaedevn::ui::UiNode& Root,
		const std::unordered_map<std::string, std::string>& StrVars,
		const std::unordered_map<std::string, double>& NumVars,
		const std::unordered_map<std::string, std::vector<kaedevn::ui::ItemScope>>& Lists)
	{
		using namespace kaedevn::ui;

		// 1) repeat 展開（{item.<key>} はここで text/action へ補間される）。
		RepeatProvider Provider;
		Provider.getList = [&Lists](const std::string& Name, std::vector<ItemScope>& Out)
		{
			const auto It = Lists.find(Name);
			if (It == Lists.end()) return false;
			Out = It->second;
			return true;
		};
		expandRepeats(Root, Provider);

		// 2) グローバル "{key}" 補間（文字列優先、無ければ数値を formatNum で）
		//    3) showIf/hideIf prune。
		VarEnv Env;
		Env.resolveStr = [&StrVars, &NumVars](const std::string& Key, std::string& Out)
		{
			const auto S = StrVars.find(Key);
			if (S != StrVars.end()) { Out = S->second; return true; }
			const auto N = NumVars.find(Key);
			if (N != NumVars.end()) { Out = formatNum(N->second); return true; }
			return false;
		};
		Env.resolveNum = kaedevn::ui::numResolver(NumVars);
		interpolateNode(Root, Env);
		applyEnabled(Root, Env);   // enabledIf 評価(repeat 補間済みリテラル/グローバル変数)
		prune(Root, Env);
	}

	// layoutRuns（UiNode.hpp・純関数）の MOMIJIUICORE_API ラッパ（別モジュールから呼ぶため）。
	std::vector<kaedevn::ui::PlacedRun> LayoutRuns(
		const std::vector<kaedevn::ui::TextRun>& Runs, int X, int Y, int WrapW, int LineH,
		int RubyHeadroom, const std::function<int(const std::string&, bool)>& Measure)
	{
		return kaedevn::ui::layoutRuns(Runs, X, Y, WrapW, LineH, RubyHeadroom, Measure);
	}
}
