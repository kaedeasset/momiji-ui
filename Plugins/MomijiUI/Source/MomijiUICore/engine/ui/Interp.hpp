// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// Interp — MomijiUI の宣言的UI に対する「変数バインド層」。UiContext(純レンダラ)
// とは独立した host 側のロジックで、以下を担う:
//   1. "{key}" テキスト補間          … interpolate / interpolateNode
//   2. showIf/hideIf による条件表示   … evalCond / isVisible / prune
//
// 意味の正は Docs/Guides/Working/momiji-ui-framework.md。ブラウザの描画器
// （Tools/momiji-ui/web/src/momiji-ui.mjs）と同じ JSON が同じ表示になることは照合で確かめる。
//
// 変数の出所(数値変数・文字列変数・作品の状態 等)は host ごとに違う
// ため、VarEnv で抽象化する。Interp 自体は出所を知らない。

#include "../core/UiNode.hpp"  // UiNode/UiType 等の宣言的モデル(描画=UiContext は不要)。MOMIJIUICORE_API は core/Export.hpp

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kaedevn::ui {

// 変数環境。host が値の出所をコールバックで与える。
struct VarEnv {
    // "{key}" 補間用: key の表示文字列を out に入れて true。未知キーは false
    //   → 呼び出し側はリテラル("{key}")を保持する(web と同じ後方互換)。
    std::function<bool(const std::string& key, std::string& out)> resolveStr;
    // showIf/hideIf 用: key の数値を out に入れて true。未設定/未対応は false
    //   → 0 として扱われる。resolveStr だけ与えて resolveNum 省略なら条件は常に偽。
    std::function<bool(const std::string& key, double& out)> resolveNum;
};

// 数の変数の表(Vars)を引く resolveNum。host が値を map で持つときの形はこれ 1 つ(vars は env を使う間生きていること)
inline std::function<bool(const std::string& key, double& out)> numResolver(const std::unordered_map<std::string, double>& vars) {
    return [&vars](const std::string& key, double& out) {
        const auto it = vars.find(key);
        if (it == vars.end()) return false;
        out = it->second;
        return true;
    };
}

// 整数値は小数点なし、それ以外は最短表記("{var}" 補間の数値整形・web と一致)。
std::string formatNum(double v);

// "{key}" を resolveStr で置換。'{' を含まなければ即返し。未知キーはリテラル保持。
std::string interpolate(const std::string& s, const VarEnv& env);
// n.text を再帰的に補間(GameHost.interpolateNode 相当)。
// MOMIJIUICORE_API: interpolateNode・prune・expandRepeats は KaedeVnCore（BuildPlayTree）が別DLLから呼ぶ（2026-10-14 分割 S1）。
MOMIJIUICORE_API void interpolateNode(UiNode& n, const VarEnv& env);

// 条件式を評価:
//   "name"          → resolveNum(name) が非0 なら true(未設定は false)
//   "name OP 数値"   → 比較(== != >= <= > <)。OP は半角。
// MOMIJIUICORE_API: UIモーションの pulse whileIf 評価で別DLL(kaedevn)から呼ぶ(07/21)。
MOMIJIUICORE_API bool evalCond(const std::string& expr, const VarEnv& env);

// 条件式の論理合成。
//   項   … evalCond の文法をそのまま使う（"name" / "name OP 数値" / 数値リテラル）
//   合成 … "!"(否定) / "&&"(論理積) / "||"(論理和) / 括弧。優先順位は ! > && > ||
//   例: "affection >= 30 && !read_ch1" / "(a > 0 || b > 0) && flag == 1"
// 項の意味論は evalCond に委譲する層分け。
// 合成演算子・括弧を含まない式は evalCond と完全に同一に振る舞う（後方互換）。
// 注: "!=" は項に属する比較演算子なので否定として解釈しない。
// 用途: UI の showIf/hideIf に加えて Op の JUMP_IF（OpRunner）が使う。
MOMIJIUICORE_API bool evalCondExpr(const std::string& expr, const VarEnv& env);
// showIf/hideIf に基づくノード単体の可視判定:
//   hideIf が真 → 隠す / showIf が偽 → 隠す / どちらも無ければ表示。
bool isVisible(const UiNode& n, const VarEnv& env);
// 不可視の子を再帰的に間引く。root 自身の可視は呼び出し側で判定。
MOMIJIUICORE_API void prune(UiNode& n, const VarEnv& env);

// enabledIf を評価して n.enabled へ反映する(再帰)。enabledIf 空は静的 enabled を
// 維持。式文法は evalCond と同一。repeat 展開後の "{item.<key>}" 補間済み文字列は
// 数値リテラル("1"/"0")になるため、evalCond はリテラルも受ける。
void applyEnabled(UiNode& n, const VarEnv& env);

// ---- 宣言的 repeat の展開(動的子) ----
// repeat="<name>" を持つノードの repeatTemplate を、provider が返す要素スコープの数だけ
// clone し、各 clone の text/action 内 "{item.<key>}" をそのスコープで補間して children に
// 展開する。provider が name のリストを返さなければ children は空のまま(repeat は解除)。
// host(PlayScreenView 等)が状態からリストを供給する。UiContext は無改修。
using ItemScope = std::unordered_map<std::string, std::string>;
struct RepeatProvider {
    // listName のリスト(要素スコープ列)を out に入れて true。無ければ false。
    std::function<bool(const std::string& listName, std::vector<ItemScope>& out)> getList;
};
MOMIJIUICORE_API void expandRepeats(UiNode& n, const RepeatProvider& provider);
// template を要素 1 つで展開する（repeat の 1 行ぶん。{item.<key>} を text/action/id/sprite/条件へ補間）。
// host が行の高さを測るときに使う（2026-10-03）。入れ子の repeat は展開しない。ブラウザの instantiateTemplate と同じ
UiNode instantiateTemplate(const UiNode& tpl, const ItemScope& scope);

// id でノードを再帰検索(JSON テンプレートの動的位置/色を host から流し込むため)。
// 見つからなければ nullptr。最初の一致を返す(id は実質一意前提)。
UiNode* findById(UiNode& root, const std::string& id);

}  // namespace kaedevn::ui
