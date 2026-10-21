// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"          // MOMIJIUICORE_API / DLLEXPORT 定義のため
#include "core/UiNode.hpp"        // kaedevn::ui::UiNode
#include "ui/Interp.hpp"          // kaedevn::ui::ItemScope（BindScreenTree の repeat 供給）

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// MomijiUI（UI フレームワーク）の公開の関数。画面 JSON の解析・寸法の一括スケール・テーマ・
// showIf/enabledIf の評価・bind・診断・リッチテキストの配置を、別モジュールから呼べるように
// MOMIJIUICORE_API で公開する（中で呼ぶ純 C++ 関数は Core ヘッダのまま）。
// ノベルの Play 画面（BuildPlayTree ほか）は KaedeVnCore.h。2026-10-14 分割 S1 で 旧 KaedeCore の公開ヘッダから分けた。
// 名前空間 KaedeCore は利用者の移行を増やさないため変えない（設計 product-split-design.md §4）。
namespace KaedeCore
{
	// 任意の画面JSON文字列を UiNode ツリーへ解析（静的な画面のプレビュー等）。
	// nodeFromJson を包み、メタ付きラッパ形式も受ける。JSON として読めなければエラーをログに出して空ノードを返し、
	// bOutOk（任意）に false を入れる。鍵の型違いは失敗にせず、その鍵だけ既定値に落とす（例外を投げない）。
	MOMIJIUICORE_API kaedevn::ui::UiNode LoadScreenTree(const std::string& Json, bool* bOutOk = nullptr);

	// 解析済みの画面 JSON から木を作る（LoadScreenTree の解析の後ろ半分）。同じ JSON から bind も読む host が
	// 2 回 parse しないための口（-KaedeUiShot・照合の自動テスト）。
	MOMIJIUICORE_API kaedevn::ui::UiNode ScreenTreeFromJson(const nlohmann::json& J);

	// 画面ツリーの寸法を一括スケールする純関数（2解像度対応。Docs/Reports/2026/07/25/02）。
	// x/w/minW/wrapW は Kx、y/h/minH は Ky、gap/pad/margin/fontPt は min(Kx,Ky) で丸める。
	// 用途: 1280x720 基準で書かれた Game/ui/*.json（タイトル/メニュー/設定…）を、論理 1920x1080 の
	// キャンバスでも同じ見かけの大きさで出す。JSON を解像度別に二重管理しないための措置。
	// Kx==Ky==1.0 は完全な no-op。fontPt=0（ホスト既定）は 0 のまま残る。
	// repeatTemplate は共有 shared_ptr なので複製してからスケールする（pristine を汚さない）。
	MOMIJIUICORE_API void ScaleUiTree(kaedevn::ui::UiNode& Node, double Kx, double Ky);

	// 任意のテーマJSON文字列を UiTheme へ解析（既定テーマをベースに上書き）。
	// JSON として読めなければエラーをログに出して既定を返し、bOutOk（任意）に false を入れる。
	MOMIJIUICORE_API kaedevn::ui::UiTheme LoadTheme(const std::string& Json, bool* bOutOk = nullptr);

	// 組み込みテーマを取得（"atelier"/"midnight"/"sakura"）。未知名は既定(atelier)を返す。
	MOMIJIUICORE_API kaedevn::ui::UiTheme BuiltinTheme(const std::string& Name);

	// showIf/hideIf を数値変数マップで評価し、不可視ノードを間引く。
	// 設定画面プレビューのタブ切替/プラットフォーム分岐（tab/touch 変数）用。
	MOMIJIUICORE_API void PruneScreenTree(kaedevn::ui::UiNode& Root,
	                                   const std::unordered_map<std::string, double>& Vars);

	// enabledIf / highlightIf を評価する（showIf の prune とは別走査）。
	// 条件式は showIf と同じ文法（&& / || / ! / 括弧つき・ui/Interp.hpp の evalCondExpr）。
	MOMIJIUICORE_API void ApplyEnabledTree(kaedevn::ui::UiNode& Root,
	                                    const std::unordered_map<std::string, double>& Vars);

	// 画面ツリーへ「repeat 展開 → {var} 補間 → showIf/hideIf prune」を一括適用する host 層
	// ヘルパ（BuildPlayTree が Play 画面に行うのと同じ手順の汎用版。放置ゲー等の状態画面用）。
	// - Lists: repeat="<name>" へ供給する要素スコープ列（{item.<key>} は text/action に補間）
	// - StrVars: "{key}" 補間の文字列変数。未ヒット時は NumVars を formatNum で文字列化して補間
	// - NumVars: showIf/hideIf の数値変数
	MOMIJIUICORE_API void BindScreenTree(
		kaedevn::ui::UiNode& Root,
		const std::unordered_map<std::string, std::string>& StrVars,
		const std::unordered_map<std::string, double>& NumVars,
		const std::unordered_map<std::string, std::vector<kaedevn::ui::ItemScope>>& Lists);

	// 画面 JSON を診断する（kaedevn::ui::validateScreen の MOMIJIUICORE_API ラッパ。2026-10-03・仕様 §12）。JSON でなければ bad-value 1 件
	MOMIJIUICORE_API std::vector<kaedevn::ui::UiDiagnostic> ValidateScreenJson(const std::string& Json);

	// template（repeat の型）を要素 1 つで展開する（kaedevn::ui::instantiateTemplate の MOMIJIUICORE_API ラッパ）。
	// host が行の高さを KaedeUI::MeasureNodeHeight で測るときに使う（2026-10-03）
	MOMIJIUICORE_API kaedevn::ui::UiNode InstantiateTemplate(const kaedevn::ui::UiNode& Tpl, const kaedevn::ui::ItemScope& Scope);

	// runs（リッチテキスト F1）を折り返し＋ルビ確保込みで配置する純関数 layoutRuns（UiNode.hpp）を、
	// 別モジュール（MomijiUISlate の KaedeUiPainter）から呼ぶための MOMIJIUICORE_API ラッパ。
	MOMIJIUICORE_API std::vector<kaedevn::ui::PlacedRun> LayoutRuns(
		const std::vector<kaedevn::ui::TextRun>& Runs, int X, int Y, int WrapW, int LineH,
		int RubyHeadroom, const std::function<int(const std::string&, bool)>& Measure);
}
