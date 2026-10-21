// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// Interp.cpp — "{var}" 補間と showIf/hideIf 条件評価の実装（データバインド層）。
//
// **同名の罠に注意: ここに「イージング」は無い。** それは ui/Easing.hpp。
// このファイルは「JSON のテキストへ変数を差し込む」「条件でノードを刈る」だけを担う。
//
// 意味論の正は Docs/Guides/Working/momiji-ui-framework.md で、
// **同じ JSON が UE とブラウザで同じ表示になる**ことを保つのが目的（照合は Scripts/momiji-ui-conformance.ps1）。
// 変数の出所は VarEnv で抽象化してあり、このファイルは出所を知らない。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "Interp.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <regex>
#include <sstream>

namespace kaedevn::ui {

// "{var}" の数値整形。整数は小数点なし、それ以外は既定の最短表記。
// (ブラウザの描画器の "{var}" と同じ見た目。)
std::string formatNum(double v) {
    if (std::isfinite(v) && std::floor(v) == v && std::abs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        return buf;
    }
    std::ostringstream os;
    os << v;
    return os.str();
}

std::string interpolate(const std::string& s, const VarEnv& env) {
    if (s.find('{') == std::string::npos) return s;
    if (!env.resolveStr) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            size_t close = s.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string key = s.substr(i + 1, close - i - 1);
                std::string val;
                if (env.resolveStr(key, val)) {  // 既知キーのみ置換
                    out += val;
                    i = close + 1;
                    continue;
                }
                // 未知キーはリテラル保持(web の m 返却と同じ)
            }
        }
        out += s[i++];
    }
    return out;
}

void interpolateNode(UiNode& n, const VarEnv& env) {
    if (!n.text.empty()) n.text = interpolate(n.text, env);
    // sprite も補間対象(Image のパス差し替え = 図鑑の連番PNG回転など)。静的パスは
    // "{" を含まないため無変換で素通りする。
    if (!n.sprite.empty()) n.sprite = interpolate(n.sprite, env);
    // action も補間対象(worldmap の "{wm_<site>_action}" 等 = BuildVars が遷移先を供給する
    // 動的ボタン)。repeat 展開(interpolateItemFields)だけが action を解決していたため、
    // repeat 外の動的 action がリテラルのままクリックされ無反応だった(07/20 実プレイで発覚・
    // worldmap-band-flow の hits アサートが回帰検出する)。未知キーはリテラル保持なので
    // "play.choice:{item.index}" 等の repeat 前テンプレートは素通りする。
    if (!n.action.empty()) n.action = interpolate(n.action, env);
    // runs の文字と絵も（2026-10-12・大小の数字 "{p0_hp}" など）
    for (auto& r : n.runs) { if (!r.text.empty()) r.text = interpolate(r.text, env); if (!r.icon.empty()) r.icon = interpolate(r.icon, env); }
    for (auto& c : n.children) interpolateNode(c, env);
}

bool evalCond(const std::string& expr, const VarEnv& env) {
    auto num = [&](const std::string& key) -> double {
        double v = 0.0;
        if (env.resolveNum && env.resolveNum(key, v)) return v;
        return 0.0;
    };
    // "name OP 数値" を厳密一致で判定(ブラウザの描画器と同じ正規表現)。
    // 名前には . を使える（2026-10-11・RPG 制作ツールの self.opened / item.potion。momiji-ui.mjs・adv-uitree.mjs も同じ）
    static const std::regex re(
        R"(^\s*([A-Za-z_][\w.]*)\s*(==|!=|>=|<=|>|<)\s*(-?\d+(?:\.\d+)?)\s*$)");
    std::smatch m;
    if (std::regex_match(expr, m, re)) {
        const double v = num(m[1].str());
        const double n = std::stod(m[3].str());
        const std::string op = m[2].str();
        if (op == "==") return v == n;
        if (op == "!=") return v != n;
        if (op == ">=") return v >= n;
        if (op == "<=") return v <= n;
        if (op == ">") return v > n;
        if (op == "<") return v < n;
    }
    // 数値リテラル単体("1"/"0"/"-2.5")は変数を引かず値の真偽。repeat 展開で
    // "{item.<key>}" が補間済みの enabledIf がここを通る(変数名は数字始まり不可
    // なので既存の変数評価とは衝突しない)。
    static const std::regex reLit(R"(^\s*(-?\d+(?:\.\d+)?)\s*$)");
    if (std::regex_match(expr, m, reLit)) return std::stod(m[1].str()) != 0.0;
    // 比較が無ければ var の真偽(非0)。web の !!this.var(expr) と同じ。
    return num(expr) != 0.0;
}

namespace {

// evalCondExpr の再帰下降パーサ。
//   or      := and ( "||" and )*
//   and     := unary ( "&&" unary )*
//   unary   := "!" unary | primary      ※ "!=" は項の比較演算子なので否定に食わない
//   primary := "(" or ")" | <項>        ※ 項は evalCond へ丸投げ（意味論を一本化）
// 項の評価に副作用が無いため短絡評価はしない（結果は同じ）。
class CondParser {
public:
    CondParser(const std::string& inExpr, const VarEnv& inEnv) : s(inExpr), env(inEnv) {}

    bool parse() { return parseOr(); }

private:
    const std::string& s;
    const VarEnv& env;
    std::size_t i = 0;

    void skipWs() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    // 現在位置が 2 文字演算子 op か（範囲外アクセスを避けるため長さを先に見る）。
    bool has(const char* op) const {
        return i + 1 < s.size() && s[i] == op[0] && s[i + 1] == op[1];
    }

    bool parseOr() {
        bool v = parseAnd();
        for (;;) {
            skipWs();
            if (!has("||")) return v;
            i += 2;
            const bool rhs = parseAnd();
            v = v || rhs;
        }
    }

    bool parseAnd() {
        bool v = parseUnary();
        for (;;) {
            skipWs();
            if (!has("&&")) return v;
            i += 2;
            const bool rhs = parseUnary();
            v = v && rhs;
        }
    }

    bool parseUnary() {
        skipWs();
        // '!' の次が '=' なら比較演算子 "!=" の一部＝項に属するので否定にしない。
        if (i < s.size() && s[i] == '!' && !(i + 1 < s.size() && s[i + 1] == '=')) {
            ++i;
            return !parseUnary();
        }
        return parsePrimary();
    }

    bool parsePrimary() {
        skipWs();
        if (i < s.size() && s[i] == '(') {
            ++i;
            const bool v = parseOr();
            skipWs();
            if (i < s.size() && s[i] == ')') ++i;  // 閉じ括弧欠落は寛容に扱う（式全体を偽にしない）
            return v;
        }
        // 項: 合成演算子・括弧の手前までを丸ごと evalCond へ渡す。
        // 比較演算子（>= <= == != > <）は項の内側なのでここでは切らない。
        const std::size_t start = i;
        while (i < s.size()) {
            if (s[i] == '(' || s[i] == ')') break;
            if (has("&&") || has("||")) break;
            ++i;
        }
        std::string term = s.substr(start, i - start);
        const std::size_t last = term.find_last_not_of(" \t\r\n");
        term = (last == std::string::npos) ? std::string() : term.substr(0, last + 1);
        if (term.empty()) return false;  // 空項（"()" や "a &&" の右辺欠落）は偽
        return evalCond(term, env);
    }
};

}  // namespace

bool evalCondExpr(const std::string& expr, const VarEnv& env) {
    // 合成演算子も括弧も無ければ evalCond と同一（旧 JUMP_IF / showIf の後方互換経路）。
    if (expr.find("&&") == std::string::npos &&
        expr.find("||") == std::string::npos &&
        expr.find('(') == std::string::npos &&
        expr.find('!') == std::string::npos) {
        return evalCond(expr, env);
    }
    return CondParser(expr, env).parse();
}

bool isVisible(const UiNode& n, const VarEnv& env) {
    // **evalCondExpr を使う（evalCond ではない）。** && / || / ! / 括弧が要る画面がある——
    // 「狭幅で、かつ 詳細を開いている」のような 2 条件を 1 ノードで書けないと、
    // 同じ意味の画面を条件の組み合わせぶん複製することになる（設計 CodexReports 08/29/03 §10.4）。
    // **既存の式は 1bit も変わらない**: evalCondExpr は && || ( ! のどれも含まない式を
    // evalCond へそのまま丸投げする（同ファイル :195）。
    if (!n.hideIf.empty() && evalCondExpr(n.hideIf, env)) return false;
    if (!n.showIf.empty() && !evalCondExpr(n.showIf, env)) return false;
    return true;
}

void prune(UiNode& n, const VarEnv& env) {
    auto& kids = n.children;
    kids.erase(std::remove_if(kids.begin(), kids.end(),
                              [&](const UiNode& c) { return !isVisible(c, env); }),
               kids.end());
    for (auto& c : kids) prune(c, env);
}

void applyEnabled(UiNode& n, const VarEnv& env) {
    // showIf と同じく evalCondExpr（合成演算子つき）。理由は isVisible のコメント。
    if (!n.enabledIf.empty()) n.enabled = evalCondExpr(n.enabledIf, env);
    // highlightIf(選択状態の統一表現 S2・07/21/02)も同じ走査で評価する(式文法は同一)。
    if (!n.highlightIf.empty()) n.highlight = evalCondExpr(n.highlightIf, env);
    for (auto& c : n.children) applyEnabled(c, env);
}

// repeat 展開時に clone の text/action 内 "{item.<key>}" を補間する(再帰)。
// interpolateNode は text のみだが、選択肢の action("play.choice:{item.index}")も
// 解決する必要があるため text と action の両方を対象にする。
static void interpolateItemFields(UiNode& n, const VarEnv& env) {
    if (!n.text.empty()) n.text = interpolate(n.text, env);
    if (!n.action.empty()) n.action = interpolate(n.action, env);
    // id も要素スコープで補間("row-{item.id}" → "row-herb1")。UIアニメの安定キーは
    // id/action から作るため、action を持たない表示専用の行はこれで同一性を得る
    //(設計 07/21/04 §A3)。
    if (!n.id.empty()) n.id = interpolate(n.id, env);
    if (!n.sprite.empty()) n.sprite = interpolate(n.sprite, env);   // {item.icon} 等のタイル画像
    if (!n.enabledIf.empty()) n.enabledIf = interpolate(n.enabledIf, env);  // "{item.can}" → "1"/"0"
    if (!n.highlightIf.empty()) n.highlightIf = interpolate(n.highlightIf, env);  // "{item.selected}" → "1"/"0"
    // showIf/hideIf も要素スコープで補間("{item.ready}" → "1"/"0" → evalCond の数値リテラル
    // 評価)。行の状態で表示を切り替えるテンプレート(例: craft スロットの「完成！回収」強調)
    // に必要。
    if (!n.showIf.empty()) n.showIf = interpolate(n.showIf, env);
    if (!n.hideIf.empty()) n.hideIf = interpolate(n.hideIf, env);
    for (auto& r : n.runs) { if (!r.text.empty()) r.text = interpolate(r.text, env); if (!r.icon.empty()) r.icon = interpolate(r.icon, env); }
    for (auto& c : n.children) interpolateItemFields(c, env);
}

void expandRepeats(UiNode& n, const RepeatProvider& provider) {
    if (!n.repeat.empty() && (n.repeatTemplate || !n.repeatTemplates.empty())) {
        std::vector<ItemScope> items;
        if (provider.getList && provider.getList(n.repeat, items)) {
            n.children.clear();
            for (const auto& scope : items) {
                // 型を選ぶ（F2）: item[templateKey] が templates にあればそれ、無ければ template。どちらも無い要素は出さない
                const UiNode* tpl = n.repeatTemplate.get();
                if (!n.templateKey.empty()) {
                    const auto k = scope.find(n.templateKey);
                    if (k != scope.end()) {
                        const auto t = n.repeatTemplates.find(k->second);
                        if (t != n.repeatTemplates.end() && t->second) tpl = t->second.get();
                    }
                }
                if (!tpl) continue;
                UiNode clone = *tpl;  // テンプレートは不変、clone を使う
                clone.fromRepeat = true;  // srcPath は template の場所のまま（UI エディタ U3）
                VarEnv itemEnv;
                itemEnv.resolveStr = [&scope](const std::string& key, std::string& out) -> bool {
                    if (key.rfind("item.", 0) == 0) {  // "item.<key>" を要素スコープから
                        auto it = scope.find(key.substr(5));
                        if (it != scope.end()) { out = it->second; return true; }
                    }
                    return false;
                };
                interpolateItemFields(clone, itemEnv);
                expandRepeats(clone, provider);  // ネストした repeat も展開
                n.children.push_back(std::move(clone));
            }
        }
        n.repeat.clear();  // 展開済み(空リストでも repeat は解除)
    } else {
        for (auto& c : n.children) expandRepeats(c, provider);
    }
}

UiNode instantiateTemplate(const UiNode& tpl, const ItemScope& scope) {
    UiNode clone = tpl;
    clone.fromRepeat = true;
    VarEnv itemEnv;
    itemEnv.resolveStr = [&scope](const std::string& key, std::string& out) -> bool {
        if (key.rfind("item.", 0) == 0) {
            auto it = scope.find(key.substr(5));
            if (it != scope.end()) { out = it->second; return true; }
        }
        return false;
    };
    interpolateItemFields(clone, itemEnv);
    return clone;
}

UiNode* findById(UiNode& root, const std::string& id) {
    if (root.id == id) return &root;
    for (auto& c : root.children)
        if (UiNode* f = findById(c, id)) return f;
    return nullptr;
}

}  // namespace kaedevn::ui
