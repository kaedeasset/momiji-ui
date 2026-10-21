// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// UiNode.cpp — core/UiNode.hpp の純関数実装。
//
// JSON <-> UiNode 変換 / テーマ読み込み / 組み込みテーマ /
// run レイアウト。描画器にも UE にも依存しない（RPG の CLI も同じ源を組み立てる）。
//
// ファイルの読み込みは host（UE は MomijiUICore.h の KaedeCore::LoadScreenTree / LoadTheme）が行う。

#include "UiNode.hpp"

#include "UiAnim.hpp"   // "anim" キー（UIモーション語彙）のパース
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace kaedevn::ui {

// ============================================================================
// JSON -> UiNode
// ============================================================================
// 型名の表（読み込みと診断の unknown-type が同じ表を見る）
static const std::unordered_map<std::string, UiType>& typeTable() {
    static const std::unordered_map<std::string, UiType> M = {
        {"Panel", UiType::Panel}, {"Image", UiType::Image}, {"Label", UiType::Label},
        {"Button", UiType::Button}, {"Slider", UiType::Slider}, {"Toggle", UiType::Toggle},
        {"ToggleGroup", UiType::ToggleGroup}, {"ScrollView", UiType::ScrollView},
        {"Modal", UiType::Modal},
        {"VerticalLayout", UiType::VerticalLayout}, {"HorizontalLayout", UiType::HorizontalLayout},
        {"GridLayout", UiType::GridLayout},
        {"TextInput", UiType::TextInput},
        {"Polyline", UiType::Polyline}, {"Ellipse", UiType::Ellipse},
        {"Polygon", UiType::Polygon}, {"Divider", UiType::Divider},
    };
    return M;
}

static UiType typeFromStr(const std::string& s) {
    const auto& M = typeTable();
    auto it = M.find(s);
    return it != M.end() ? it->second : UiType::Panel;
}

// "#rrggbb" / "#rrggbbaa" を解析。失敗時は false。
static bool parseHexColor(const std::string& s, Color& out) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    auto hx = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto byte = [&](size_t i) -> int {
        int hi = hx(s[i]), lo = hx(s[i + 1]);
        return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
    };
    int r = byte(1), g = byte(3), b = byte(5);
    int a = (s.size() == 9) ? byte(7) : 255;
    if (r < 0 || g < 0 || b < 0 || a < 0) return false;
    out = {static_cast<uint8_t>(r), static_cast<uint8_t>(g),
           static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
    return true;
}

// ---- 2026-10-12 の描き方の読み込み（部品とテーマで共通）----
namespace {
    bool readInts(const nlohmann::json& j, const char* k, int* out, int n) {
        if (!j.contains(k) || !j[k].is_array() || (int)j[k].size() != n) return false;
        for (int i = 0; i < n; ++i) { if (!j[k][i].is_number()) return false; out[i] = j[k][i].get<int>(); }
        return true;
    }
    bool readColorKey(const nlohmann::json& o, const char* k, Color& dst) {
        return o.contains(k) && o[k].is_string() && parseHexColor(o[k].get<std::string>(), dst);
    }
    // {"dir":"v"|"h", "stops":[["#c", pos], …]}
    bool readGradientObj(const nlohmann::json& g, UiGradient& out) {
        if (!g.is_object() || !g.contains("stops") || !g["stops"].is_array()) return false;
        UiGradient r;
        r.horizontal = g.contains("dir") && g["dir"].is_string() && g["dir"].get<std::string>() == "h";
        for (const auto& st : g["stops"]) {
            if (!st.is_array() || st.size() != 2 || !st[0].is_string() || !st[1].is_number()) continue;
            Color c; if (!parseHexColor(st[0].get<std::string>(), c)) continue;
            r.stops.push_back({c, std::max(0.f, std::min(1.f, st[1].get<float>()))});
        }
        if (r.stops.size() < 2) return false;
        r.on = true; out = r; return true;
    }
    bool readGradient(const nlohmann::json& j, const char* k, UiGradient& out) { return j.contains(k) && readGradientObj(j[k], out); }
    void readEdge3d(const nlohmann::json& j, const char* k, UiEdge3d& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k]; out.on = true;
        readColorKey(o, "light", out.light); readColorKey(o, "dark", out.dark);
        if (o.contains("width") && o["width"].is_number()) out.width = std::max(1, o["width"].get<int>());
    }
    void readInnerBorder(const nlohmann::json& j, const char* k, UiInnerBorder& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k]; out.on = true;
        readColorKey(o, "color", out.color);
        if (o.contains("width") && o["width"].is_number()) out.width = std::max(1, o["width"].get<int>());
        if (o.contains("inset") && o["inset"].is_number()) out.inset = o["inset"].get<int>();
    }
    void readTextOutline(const nlohmann::json& j, const char* k, UiTextOutline& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k]; out.on = true;
        readColorKey(o, "color", out.color);
        if (o.contains("width") && o["width"].is_number()) out.width = std::max(0, o["width"].get<int>());
        if (out.width == 0) out.on = false;
    }
    void readTextGlow(const nlohmann::json& j, const char* k, UiTextGlow& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k]; out.on = true;
        readColorKey(o, "color", out.color);
        if (o.contains("size") && o["size"].is_number()) out.size = std::max(1, o["size"].get<int>());
    }
    void readGlow(const nlohmann::json& j, const char* k, UiGlow& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k]; out.on = true;
        readColorKey(o, "color", out.color);
        if (o.contains("size") && o["size"].is_number()) out.size = std::max(1, o["size"].get<int>());
    }
    void readSpriteRef(const nlohmann::json& j, const char* k, UiSpriteRef& out) {
        if (!j.contains(k) || !j[k].is_object()) return;
        const auto& o = j[k];
        if (o.contains("sprite") && o["sprite"].is_string()) out.sprite = o["sprite"].get<std::string>();
        readInts(o, "slice", out.slice, 4);
        if (o.contains("over") && o["over"].is_boolean()) out.over = o["over"].get<bool>();
        if (o.contains("opacity") && o["opacity"].is_number()) out.opacity = o["opacity"].get<float>();
        if (o.contains("w") && o["w"].is_number()) out.w = o["w"].get<int>();
        if (o.contains("h") && o["h"].is_number()) out.h = o["h"].get<int>();
        if (o.contains("dx") && o["dx"].is_number()) out.dx = o["dx"].get<int>();
        if (o.contains("dy") && o["dy"].is_number()) out.dy = o["dy"].get<int>();
    }
}

template <class T> T opt(const nlohmann::json& j, const char* k, T def) {
    if (!j.is_object()) return def;
    const auto it = j.find(k);
    if (it == j.end()) return def;
    if constexpr (std::is_same_v<T, std::string>) { if (!it->is_string()) return def; }
    else if constexpr (std::is_same_v<T, bool>) { if (!it->is_boolean()) return def; }
    else { if (!it->is_number()) return def; }
    return it->template get<T>();
}
template int opt<int>(const nlohmann::json&, const char*, int);
template float opt<float>(const nlohmann::json&, const char*, float);
template bool opt<bool>(const nlohmann::json&, const char*, bool);
template std::string opt<std::string>(const nlohmann::json&, const char*, std::string);

// JSON Pointer の 1 段（RFC 6901: "~" は "~0"、"/" は "~1"）
static std::string pointerToken(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '~') o += "~0"; else if (c == '/') o += "~1"; else o += c; }
    return o;
}

UiNode nodeFromJson(const nlohmann::json& j, const std::string& path) {
    UiNode n;
    n.srcPath = path;
    n.type = typeFromStr(opt(j, "type", std::string("Panel")));
    n.id = opt(j, "id", std::string());
    n.x = opt(j, "x", 0); n.y = opt(j, "y", 0);
    n.w = opt(j, "w", 0); n.h = opt(j, "h", 0);
    n.gap = opt(j, "gap", 8); n.pad = opt(j, "pad", 0);
    n.margin = opt(j, "margin", 0); n.cols = opt(j, "cols", 1);
    n.flex = opt(j, "flex", 0.0f);
    n.minW = opt(j, "minW", 0); n.minH = opt(j, "minH", 0);
    n.anchor = opt(j, "anchor", std::string());
    n.showIf = opt(j, "showIf", std::string());
    n.hideIf = opt(j, "hideIf", std::string());
    n.enabledIf = opt(j, "enabledIf", std::string());
    n.highlightIf = opt(j, "highlightIf", std::string());
    n.wrapW = opt(j, "wrapW", 0);
    n.lineH = opt(j, "lineH", 0);   // 0=host 既定（fontPt からの比で算出）
    n.ellipsis = opt(j, "ellipsis", false);
    n.text = opt(j, "text", std::string());
    n.action = opt(j, "action", std::string());
    n.variant = opt(j, "variant", std::string());
    n.enabled = opt(j, "enabled", true);
    n.focusable = opt(j, "focusable", true);
    n.center = opt(j, "center", false);
    n.highlight = opt(j, "highlight", false);
    n.selectable = opt(j, "selectable", false);
    n.align = opt(j, "align", std::string());
    n.font = opt(j, "font", std::string());
    n.fontPt = opt(j, "fontSize", 0);
    n.sprite = opt(j, "sprite", std::string());
    n.fit = opt(j, "fit", std::string());
    n.icon = opt(j, "icon", std::string());
    n.value = opt(j, "value", 0.0f);
    n.on = opt(j, "on", false);
    n.selected = opt(j, "selected", 0);
    // dim は bool だが、ブラウザの描画器は `n.dim !== false` の truthy 判定のため
    // 数値(不透明度風の 0.55 等)で書かれた JSON も通る。C++ 側で get<bool> が
    // 例外を投げて画面全体が読めなくなるのを避け、数値は非0=true として受ける。
    if (j.contains("dim")) {
        const auto& d = j["dim"];
        if (d.is_boolean()) n.dim = d.get<bool>();
        else if (d.is_number()) n.dim = d.get<double>() != 0.0;
    }
    n.placeholder = opt(j, "placeholder", std::string());
    n.bindVar = opt(j, "bindVar", std::string());
    n.maxLen = opt(j, "maxLen", 0);
    n.repeat = opt(j, "repeat", std::string());
    // ---- Polyline / Ellipse(F-2) ----
    // points は [[x,y], ...] を受ける。壊れた要素は黙って飛ばす(画面全体を落とさない)。
    n.thickness = opt(j, "thickness", 1.0f);
    n.closed = opt(j, "closed", false);
    n.fill = opt(j, "fill", true);
    if (j.contains("points") && j["points"].is_array()) {
        for (const auto& pj : j["points"]) {
            if (!pj.is_array() || pj.size() < 2) continue;
            if (!pj[0].is_number() || !pj[1].is_number()) continue;
            n.points.push_back(UiPoint{pj[0].get<int>(), pj[1].get<int>()});
        }
    }
    if (j.contains("template") && j["template"].is_object())
        n.repeatTemplate = std::make_shared<UiNode>(nodeFromJson(j["template"], path + "/template"));
    if (j.contains("templates") && j["templates"].is_object()) {
        for (auto it = j["templates"].begin(); it != j["templates"].end(); ++it)
            if (it.value().is_object())
                n.repeatTemplates[it.key()] = std::make_shared<UiNode>(nodeFromJson(it.value(), path + "/templates/" + pointerToken(it.key())));
    }
    n.templateKey = opt(j, "templateKey", std::string());
    if (j.contains("anim")) n.anim = animFromJson(j["anim"]);

    if (j.contains("color") && j["color"].is_string()) {
        Color c; if (parseHexColor(j["color"].get<std::string>(), c)) { n.color = c; n.hasColor = true; }
    }
    // ---- 2026-10-12 の描き方 ----
    readGradient(j, "gradient", n.gradient);
    if (j.contains("shape") && j["shape"].is_string()) n.shape = j["shape"].get<std::string>();
    if (j.contains("shapeInset") && j["shapeInset"].is_number()) n.shapeInset = j["shapeInset"].get<int>();
    readEdge3d(j, "edge3d", n.edge3d);
    readInnerBorder(j, "innerBorder", n.innerBorder);
    if (readInts(j, "slice", n.slice, 4)) n.hasSlice = true;
    if (j.contains("tile") && j["tile"].is_boolean()) n.tile = j["tile"].get<bool>();
    if (readInts(j, "src", n.src, 4)) n.hasSrc = true;
    if (j.contains("ornament") && j["ornament"].is_string()) n.ornament = j["ornament"].get<std::string>();
    readTextOutline(j, "textOutline", n.textOutline);
    readTextGlow(j, "textGlow", n.textGlow);
    if (j.contains("letterSpacing") && j["letterSpacing"].is_number()) n.letterSpacing = j["letterSpacing"].get<float>();
    if (j.contains("monoDigits") && j["monoDigits"].is_boolean()) n.monoDigits = j["monoDigits"].get<bool>();
    if (j.contains("highlightTextColor") && j["highlightTextColor"].is_string()) {
        Color c; if (parseHexColor(j["highlightTextColor"].get<std::string>(), c)) { n.highlightTextColor = c; n.hasHighlightTextColor = true; }
    }
    readInts(j, "offset", n.offset, 2);
    if (j.contains("rotate") && j["rotate"].is_number()) n.rotate = j["rotate"].get<float>();
    if (j.contains("skewX") && j["skewX"].is_number()) n.skewX = j["skewX"].get<float>();
    if (j.contains("clip") && j["clip"].is_boolean()) n.clip = j["clip"].get<bool>();
    if (j.contains("opacity") && j["opacity"].is_number()) n.opacity = std::max(0.f, std::min(1.f, j["opacity"].get<float>()));
    readGlow(j, "glow", n.glow);
    if (j.contains("backdropBlur") && j["backdropBlur"].is_number()) n.backdropBlur = j["backdropBlur"].get<float>();
    if (j.contains("borderColor") && j["borderColor"].is_string()) {
        Color c; if (parseHexColor(j["borderColor"].get<std::string>(), c)) { n.borderColor = c; n.hasBorderColor = true; }
    }
    if (j.contains("borderWidth") && j["borderWidth"].is_number()) n.borderWidth = std::max(0, j["borderWidth"].get<int>());
    if (j.contains("textColor") && j["textColor"].is_string()) {
        Color c; if (parseHexColor(j["textColor"].get<std::string>(), c)) {
            n.textColor = {c.r, c.g, c.b, c.a}; n.hasTextColor = true;
        }
    }
    if (j.contains("runs") && j["runs"].is_array()) {
        for (const auto& rj : j["runs"]) {
            if (!rj.is_object()) continue;
            TextRun run;
            run.text = opt(rj, "text", std::string());
            run.bold = opt(rj, "bold", false);
            run.ruby = opt(rj, "ruby", std::string());
            if (rj.contains("size") && rj["size"].is_number()) run.size = rj["size"].get<int>();
            if (rj.contains("icon") && rj["icon"].is_string()) run.icon = rj["icon"].get<std::string>();
            readInts(rj, "src", run.iconSrc, 4);
            if (rj.contains("color") && rj["color"].is_string()) {
                Color c;
                if (parseHexColor(rj["color"].get<std::string>(), c)) {
                    run.color = {c.r, c.g, c.b, c.a}; run.hasColor = true;
                }
            }
            n.runs.push_back(std::move(run));
        }
    }
    if (j.contains("children") && j["children"].is_array()) {
        int i = 0;
        for (const auto& c : j["children"]) n.children.push_back(nodeFromJson(c, path + "/children/" + std::to_string(i++)));
    }
    return n;
}

UiNode screenFromJsonString(const std::string& s) {
    return nodeFromJson(nlohmann::json::parse(s));
}

// ============================================================================
// 診断（validateScreen）。ブラウザの MomijiUICanvas.validate と同じ表・同じ順で見る
// ============================================================================
namespace {
    const std::unordered_set<std::string>& vNodeKeys() {
        static const std::unordered_set<std::string> S = {"type","id","x","y","w","h","gap","pad","margin","cols","flex","minW","minH","anchor",
            "showIf","hideIf","highlightIf","enabledIf","text","action","variant","enabled","focusable","center","highlight",
            "selectable","color","textColor","borderColor","borderWidth","align","font","fontSize","wrapW","lineH","ellipsis","runs","placeholder","bindVar","maxLen",
            "focused","sprite","fit","icon","value","on","selected","dim","points","thickness",
            "closed","fill","anim","repeat","template","templates","templateKey","children",
            "gradient","shape","shapeInset","edge3d","innerBorder","slice","tile","src","ornament","textOutline","textGlow","letterSpacing",
            "monoDigits","highlightTextColor","offset","rotate","skewX","clip","opacity","glow","backdropBlur"};
        return S;
    }
    const std::unordered_set<std::string>& vDocKeys() { static const std::unordered_set<std::string> S = {"id","note","title","root","bind","scenarios","invalid","host"}; return S; }   // host: 画面を使う側の数値（描画器は見ない）
    const std::unordered_set<std::string>& vNum() { static const std::unordered_set<std::string> S = {"x","y","w","h","gap","pad","margin","cols","flex","minW","minH","fontSize","wrapW","lineH","maxLen","value","selected","thickness","borderWidth","shapeInset","letterSpacing","rotate","skewX","opacity","backdropBlur"}; return S; }
    const std::unordered_set<std::string>& vStr() { static const std::unordered_set<std::string> S = {"type","id","anchor","showIf","hideIf","highlightIf","enabledIf","text","action","variant","color","textColor","borderColor","highlightTextColor","shape","ornament","align","font","placeholder","bindVar","sprite","fit","icon","repeat","templateKey"}; return S; }
    const std::unordered_set<std::string>& vBool() { static const std::unordered_set<std::string> S = {"enabled","focusable","center","highlight","selectable","ellipsis","focused","on","closed","fill","tile","monoDigits","clip"}; return S; }
    const std::unordered_set<std::string>& vAnimKeys() { static const std::unordered_set<std::string> S = {"enter","countUp","pulse","staggerMs","blink","bob","frames","exit","tween"}; return S; }
    const std::vector<std::string>* vEnum(const std::string& k) {
        static const std::vector<std::string> A = {"fill","safe","top","bottom","left","right","center"}, F = {"contain","cover","stretch"}, L = {"left","center","right"};
        return k == "anchor" ? &A : k == "fit" ? &F : k == "align" ? &L : nullptr;
    }
    bool vIsColor(const std::string& s) {
        if (s.size() != 7 && s.size() != 9) return false;
        if (s[0] != '#') return false;
        for (size_t i = 1; i < s.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        return true;
    }
    std::string vJoin(const std::vector<std::string>& v) { std::string o; for (size_t i = 0; i < v.size(); ++i) { if (i) o += "|"; o += v[i]; } return o; }

    struct Validator {
        std::vector<UiDiagnostic> out;
        std::unordered_map<std::string, std::pair<std::string, bool>> ids;   // id → (path, 状態を持つか)
        void push(const char* sev, const char* code, const std::string& path, const std::string& msg) { out.push_back({sev, code, path, msg}); }
        void node(const nlohmann::json& n, const std::string& path) {
            if (!n.is_object()) { push("error", "bad-value", path, "部品はオブジェクト"); return; }
            const std::string type = n.contains("type") ? (n["type"].is_string() ? n["type"].get<std::string>() : std::string("\x01")) : std::string("Panel");
            if (!typeTable().count(type)) push("error", "unknown-type", path + "/type", "知らない type: " + n["type"].dump());
            for (auto it = n.begin(); it != n.end(); ++it) {
                const std::string& k = it.key();
                if (!k.empty() && k[0] == '_') continue;
                const nlohmann::json& v = it.value();
                const std::string p = path + "/" + k;
                if (!vNodeKeys().count(k)) { push("warn", "unknown-key", p, "知らない鍵（無視される）: " + k); continue; }
                if (vNum().count(k) && !v.is_number()) push("error", "bad-value", p, k + " は数");
                else if (vStr().count(k) && !v.is_string()) push("error", "bad-value", p, k + " は文字列");
                else if (vBool().count(k) && !v.is_boolean()) push("error", "bad-value", p, k + " は真偽");
                else if (k == "dim" && !v.is_boolean() && !v.is_number()) push("error", "bad-value", p, "dim は真偽か数");
                else if ((k == "children" || k == "runs" || k == "points") && !v.is_array()) push("error", "bad-value", p, k + " は配列");
                else if ((k == "template" || k == "templates" || k == "anim") && !v.is_object()) push("error", "bad-value", p, k + " はオブジェクト");
                else if (const auto* e = vEnum(k); e && v.is_string() && std::find(e->begin(), e->end(), v.get<std::string>()) == e->end()) push("error", "bad-value", p, k + " は " + vJoin(*e));
                else if ((k == "color" || k == "textColor" || k == "borderColor") && v.is_string() && v.get<std::string>().rfind("$", 0) != 0 && !vIsColor(v.get<std::string>())) push("warn", "bad-color", p, "色は #rrggbb か #rrggbbaa（無視されて既定の色になる）: " + v.get<std::string>());
            }
            // selectable は器（Panel / VerticalLayout / ScrollView）だけ。ほかに書いても選べない（2026-10-07）
            if (n.contains("selectable") && type != "Panel" && type != "VerticalLayout" && type != "ScrollView") push("warn", "selectable-ignored", path + "/selectable", "selectable は Panel / VerticalLayout / ScrollView だけ（無視される）");
            if (n.contains("id") && n["id"].is_string()) {
                const std::string id = n["id"].get<std::string>();
                if (!id.empty() && id.find('{') == std::string::npos) {
                    // 状態を持つ部品（押せる・文字欄・ScrollView・Modal）の id の重複は焦点やスクロール量の鍵が曖昧になる＝error。見た目だけの部品は warn
                    const bool stateful = (n.contains("action") && n["action"].is_string() && !n["action"].get<std::string>().empty())
                        || (n.contains("bindVar") && n["bindVar"].is_string() && !n["bindVar"].get<std::string>().empty())
                        || type == "ScrollView" || type == "Modal";
                    auto f = ids.find(id);
                    if (f != ids.end()) push((stateful || f->second.second) ? "error" : "warn", "dup-id", path + "/id", "id が重複: " + id + "（先: " + f->second.first + "）");
                    else ids[id] = {path, stateful};
                }
            }
            const bool hasTpl = n.contains("template") && n["template"].is_object();
            const bool hasTpls = n.contains("templates") && n["templates"].is_object();
            if (n.contains("repeat") && n["repeat"].is_string() && !n["repeat"].get<std::string>().empty() && !hasTpl && !hasTpls) push("error", "template-missing", path + "/repeat", "repeat には template か templates が要る");
            if (n.contains("templateKey") && n["templateKey"].is_string() && !hasTpls) push("error", "template-missing", path + "/templateKey", "templateKey には templates が要る");
            if (n.contains("runs") && n["runs"].is_array()) { int i = 0; for (const auto& r : n["runs"]) { if (!r.is_object() || !r.contains("text") || !r["text"].is_string()) push("error", "bad-value", path + "/runs/" + std::to_string(i), "runs の要素は {text, bold?, color?, ruby?}"); ++i; } }
            if (n.contains("points") && n["points"].is_array()) { int i = 0; for (const auto& pt : n["points"]) { if (!pt.is_array() || pt.size() < 2 || !pt[0].is_number() || !pt[1].is_number()) push("error", "bad-value", path + "/points/" + std::to_string(i), "points の要素は [x, y]"); ++i; } }
            if (n.contains("anim") && n["anim"].is_object()) {
                for (auto it = n["anim"].begin(); it != n["anim"].end(); ++it) { const std::string& k = it.key(); if (!k.empty() && k[0] != '_' && !vAnimKeys().count(k)) push("warn", "unknown-key", path + "/anim/" + k, "anim の知らない鍵（無視される）: " + k); }
                const nlohmann::json& a = n["anim"];
                if (a.contains("enter") && a["enter"].is_object() && a["enter"].contains("type") && a["enter"]["type"].is_string()) {
                    const std::string t = a["enter"]["type"].get<std::string>();
                    if (t != "fade" && t != "slide" && t != "pop") push("error", "bad-value", path + "/anim/enter/type", "enter.type は fade|slide|pop");
                }
            }
            if (hasTpl) node(n["template"], path + "/template");
            if (hasTpls) for (auto it = n["templates"].begin(); it != n["templates"].end(); ++it) node(it.value(), path + "/templates/" + it.key());
            if (n.contains("children") && n["children"].is_array()) { int i = 0; for (const auto& c : n["children"]) node(c, path + "/children/" + std::to_string(i++)); }
        }
    };
}

std::vector<UiDiagnostic> validateScreen(const nlohmann::json& doc) {
    Validator v;
    if (!doc.is_object()) { v.push("error", "bad-value", "", "画面はオブジェクト"); return v.out; }
    if (doc.contains("root") && doc["root"].is_object()) {
        for (auto it = doc.begin(); it != doc.end(); ++it) { const std::string& k = it.key(); if (!k.empty() && k[0] != '_' && !vDocKeys().count(k)) v.push("warn", "unknown-key", "/" + k, "知らない鍵（無視される）: " + k); }
        v.node(doc["root"], "/root");
    } else v.node(doc, "");
    return v.out;
}

// ============================================================================
// テーマ(スキン)
// ============================================================================
UiTheme themeFromJson(const nlohmann::json& j, const UiTheme& base) {
    UiTheme t = base;
    auto fill = [&](const char* key, Color& dst) {
        if (j.contains(key) && j[key].is_string()) {
            Color c; if (parseHexColor(j[key].get<std::string>(), c)) dst = c;
        }
    };
    fill("bg", t.bg);
    fill("panel", t.panel);
    fill("panelLight", t.panelLight);
    fill("panelEdge", t.panelEdge);
    fill("accent", t.accent);
    fill("accentDim", t.accentDim);
    fill("track", t.track);
    fill("dim", t.dim);
    fill("textPrimary", t.textPrimary);
    fill("textDim", t.textDim);
    fill("textOnAccent", t.textOnAccent);
    if (j.contains("radius") && j["radius"].is_number()) t.radius = j["radius"].get<int>();
    if (j.contains("border") && j["border"].is_number()) t.border = std::max(1, j["border"].get<int>());
    if (j.contains("frame") && j["frame"].is_string()) t.frame = j["frame"].get<std::string>();
    auto num = [](const nlohmann::json& o, const char* k, int& dst) { if (o.contains(k) && o[k].is_number()) dst = o[k].get<int>(); };
    if (j.contains("shadow") && j["shadow"].is_object()) {
        const auto& o = j["shadow"];
        num(o, "x", t.shadowX); num(o, "y", t.shadowY); num(o, "blur", t.shadowBlur);
        if (o.contains("color") && o["color"].is_string()) parseHexColor(o["color"].get<std::string>(), t.shadowColor);
    }
    fill("gradient", t.gradient);
    fill("bevel", t.bevel);
    if (j.contains("textShadow") && j["textShadow"].is_object()) {
        const auto& o = j["textShadow"];
        num(o, "x", t.textShadowX); num(o, "y", t.textShadowY);
        if (o.contains("color") && o["color"].is_string()) parseHexColor(o["color"].get<std::string>(), t.textShadow);
    }
    if (j.contains("cursorBar") && j["cursorBar"].is_number()) t.cursorBar = std::max(0, j["cursorBar"].get<int>());
    // ---- 7 テーマの描き方 ----
    if (j.contains("gradient") && j["gradient"].is_object()) readGradientObj(j["gradient"], t.windowGradient);
    readEdge3d(j, "edge3d", t.edge3d);
    readInnerBorder(j, "innerBorder", t.innerBorder);
    readSpriteRef(j, "frameImage", t.frameImage);
    readSpriteRef(j, "pattern", t.pattern);
    if (j.contains("windowShape") && j["windowShape"].is_string()) t.windowShape = j["windowShape"].get<std::string>();
    if (j.contains("select") && j["select"].is_object()) {
        const auto& o = j["select"]; t.select.on = true;
        readGradient(o, "gradient", t.select.gradient);
        t.select.hasFill = readColorKey(o, "fill", t.select.fill);
        t.select.hasBorder = readColorKey(o, "border", t.select.border);
        t.select.hasText = readColorKey(o, "text", t.select.text);
        readSpriteRef(o, "cursor", t.select.cursor);
        readGlow(o, "glow", t.select.glow);
    }
    readTextOutline(j, "textOutline", t.textOutline);
    readTextGlow(j, "textGlow", t.textGlow);
    if (j.contains("letterSpacing") && j["letterSpacing"].is_number()) t.letterSpacing = j["letterSpacing"].get<float>();
    if (j.contains("knockout") && j["knockout"].is_boolean()) t.knockout = j["knockout"].get<bool>();
    return t;
}

bool builtinTheme(const std::string& name, UiTheme& out) {
    if (name == "atelier" || name.empty()) {
        out = UiTheme{};  // 既定(暖色・アトリエの灯)
        return true;
    }
    if (name == "midnight") {  // 寒色・夜間の SF/ミステリ向け
        out = UiTheme{};
        out.bg = {0x0e, 0x11, 0x16, 255};
        out.panel = {0x18, 0x1f, 0x2b, 255};
        out.panelLight = {0x22, 0x2b, 0x3a, 255};
        out.panelEdge = {0x33, 0x40, 0x55, 255};
        out.accent = {0x5a, 0xb0, 0xff, 255};
        out.accentDim = {0x33, 0x60, 0x8c, 255};
        out.track = {0x09, 0x0c, 0x12, 230};
        out.textPrimary = {0xe6, 0xed, 0xf6, 0xff};
        out.textDim = {0x9a, 0xa6, 0xb8, 0xff};
        out.textOnAccent = {0x0e, 0x11, 0x16, 0xff};
        return true;
    }
    if (name == "sakura") {  // 桜色・恋愛/日常向け
        out = UiTheme{};
        out.bg = {0x2a, 0x1e, 0x25, 255};
        out.panel = {0x3b, 0x2a, 0x33, 255};
        out.panelLight = {0x4d, 0x37, 0x42, 255};
        out.panelEdge = {0x6a, 0x4a, 0x59, 255};
        out.accent = {0xff, 0x9d, 0xc0, 255};
        out.accentDim = {0xa8, 0x63, 0x7e, 255};
        out.track = {0x1d, 0x14, 0x19, 230};
        out.textPrimary = {0xfa, 0xed, 0xf2, 0xff};
        out.textDim = {0xc9, 0xb0, 0xbb, 0xff};
        out.textOnAccent = {0x2a, 0x1e, 0x25, 0xff};
        return true;
    }
    return false;
}

// ============================================================================
// run レイアウト(本文リッチテキスト F1)
// ============================================================================
std::vector<PlacedRun> layoutRuns(const std::vector<TextRun>& runs, int x, int y,
                                  int wrapW, int lineH, int rubyHeadroom,
                                  const std::function<int(const std::string&, bool)>& measure) {
    std::vector<PlacedRun> out;
    int curX = x, curY = y;
    const int right = x + (wrapW > 0 ? wrapW : 1000000);
    auto place = [&](const std::string& t, const TextRun& r, bool keepRuby) {
        const int w = measure(t, r.bold);
        if (curX > x && curX + w > right) { curX = x; curY += lineH; }  // 行頭以外で溢れたら改行
        PlacedRun p;
        p.text = t; p.x = curX; p.y = curY + rubyHeadroom;
        p.bold = r.bold; p.hasColor = r.hasColor; p.color = r.color; p.width = w;
        if (keepRuby) p.ruby = r.ruby;
        out.push_back(std::move(p));
        curX += w;
    };
    for (const auto& r : runs) {
        if (!r.ruby.empty()) {
            place(r.text, r, true);  // ルビ run は割らない(furigana が割れないように 1 片)
        } else {
            for (size_t i = 0; i < r.text.size();) {  // 非ルビは CJK 折り返しのため 1 文字ずつ
                int len = utf8CharLen(static_cast<unsigned char>(r.text[i]));
                if (i + static_cast<size_t>(len) > r.text.size())
                    len = static_cast<int>(r.text.size() - i);
                place(r.text.substr(i, static_cast<size_t>(len)), r, false);
                i += static_cast<size_t>(len);
            }
        }
    }
    return out;
}

}  // namespace kaedevn::ui
