// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// UiAnim — UI JSON の "anim" キー（UIモーション語彙）の宣言モデル。
//
// UiNode と同じ「契約」層: UE とブラウザで同じ JSON が同じモーションを生むための
// スキーマ定義であり、時間・状態は持たない（それは ui/Tween.hpp の UiMotionState）。
// 設計: Docs/Reports/2026/07/21/01（M1 enter/stagger・M2 countUp/pulse）。
// camelCase・durationMs 単位・〜If 条件は evalCond 文法。
//
// easing 名は文字列のまま保持する（11種。解決は ui/Easing.hpp の
// easingFromString。core→ui の依存を作らないため enum へはここで落とさない）。
//
// JSON 形:
//   "anim": {
//     "enter":   { "type": "fade|slide|pop", "from": "left|right|top|bottom",
//                  "distance": 40, "durationMs": 220, "delayMs": 0, "easing": "easeOutCubic" },
//     "staggerMs": 40,                        // repeat 行の index ごとに delay へ加算
//     "countUp": { "durationMs": 400, "easing": "easeOutQuad" },
//     "pulse":   { "scale": 1.06, "periodMs": 900, "whileIf": "slotReady" }
//   }

#include <nlohmann/json_fwd.hpp>   // json.hpp は UiAnim.cpp が include する
#include <memory>
#include <string>

namespace kaedevn::ui {

struct UiAnimSpec {
    // ---- enter（出現）。enterType 空 = enter なし ----
    std::string enterType;            // "fade" | "slide" | "pop"
    std::string enterFrom = "bottom"; // slide の出現方向
    float enterDistance = 40.f;       // slide の移動量（論理px）
    int   enterDurationMs = 220;
    int   enterDelayMs = 0;
    std::string enterEasing;          // 空 = 既定（fade=easeOutQuad / slide,pop=easeOutCubic）
    int   staggerMs = 0;              // repeat 行 index × これ を delay に加算

    // ---- countUp（バインド数値の補間表示）----
    bool  hasCountUp = false;
    int   countUpDurationMs = 400;
    std::string countUpEasing = "easeOutQuad";

    // ---- pulse（条件が真の間のスケール脈動）----
    bool  hasPulse = false;
    float pulseScale = 1.06f;
    int   pulsePeriodMs = 900;
    std::string pulseWhileIf;         // 空 = 常時。evalCond 文法（Interp.hpp）

    // ---- 2026-10-12（UE 先行・rpg-themes/README.md §3.3）----
    // enter.type に "open"（縦に中心から伸びる）を足した
    bool  hasBlink = false; int blinkPeriodMs = 800; float blinkMin = 0.25f;   // 不透明度を min〜1 で繰り返す
    bool  hasBob = false;   int bobPeriodMs = 900;   float bobDistance = 4.f;  // 上下に揺らす
    bool  hasFrames = false; int framesCount = 4;    int framesPeriodMs = 600; // src の切り出しを横に 1 コマずつ
    std::string exitType;   int exitDurationMs = 180;                         // "fade" | "close"（消えるときに描き続ける）
    bool  hasTween = false; int tweenDurationMs = 300;                        // Slider の棒の長さを補間

    bool hasEnter() const { return !enterType.empty(); }
    bool hasExit() const { return !exitType.empty(); }
    bool hasLoop() const { return hasBlink || hasBob || hasFrames; }
    bool any() const { return hasEnter() || hasCountUp || hasPulse || hasLoop() || hasExit() || hasTween; }
};

// "anim" の JSON オブジェクト → spec。未知キーは無視・不正型は既定値（例外は投げない）。
// 有効な内容が何も無ければ nullptr。
std::shared_ptr<UiAnimSpec> animFromJson(const nlohmann::json& j);

}  // namespace kaedevn::ui
