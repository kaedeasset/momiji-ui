// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// UiAnim.cpp — UI JSON の "anim" キーを UiAnimSpec へ読む純関数。
//
// 契約層なので**時間も状態も持たない**（実行時状態は ui/Tween.hpp の UiMotionState）。
// 未知キーは無視、不正な型は既定値へ落とし（鍵の読みは全部 UiNode.hpp の opt）、**例外は投げない**。
// UI JSON は手書きなので、記述ミス1つで画面が出なくなる方が損だという判断。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "UiAnim.hpp"
#include "UiNode.hpp"   // opt（鍵が無いか型が合わなければ既定値）
#include <nlohmann/json.hpp>

#include <algorithm>

namespace kaedevn::ui {

std::shared_ptr<UiAnimSpec> animFromJson(const nlohmann::json& j)
{
    if (!j.is_object()) return nullptr;
    auto Spec = std::make_shared<UiAnimSpec>();

    if (j.contains("enter") && j["enter"].is_object()) {
        const auto& e = j["enter"];
        Spec->enterType = opt(e, "type", std::string());
        Spec->enterFrom = opt(e, "from", std::string("bottom"));
        Spec->enterDistance = opt(e, "distance", 40.0f);
        Spec->enterDurationMs = opt(e, "durationMs", 220);
        Spec->enterDelayMs = opt(e, "delayMs", 0);
        Spec->enterEasing = opt(e, "easing", std::string());
    }
    Spec->staggerMs = opt(j, "staggerMs", 0);

    if (j.contains("countUp") && j["countUp"].is_object()) {
        const auto& c = j["countUp"];
        Spec->hasCountUp = true;
        Spec->countUpDurationMs = opt(c, "durationMs", 400);
        Spec->countUpEasing = opt(c, "easing", std::string("easeOutQuad"));
    }

    if (j.contains("pulse") && j["pulse"].is_object()) {
        const auto& p = j["pulse"];
        Spec->hasPulse = true;
        Spec->pulseScale = opt(p, "scale", 1.06f);
        Spec->pulsePeriodMs = opt(p, "periodMs", 900);
        Spec->pulseWhileIf = opt(p, "whileIf", std::string());
    }

    if (j.contains("blink") && j["blink"].is_object()) {
        const auto& b = j["blink"];
        Spec->hasBlink = true;
        Spec->blinkPeriodMs = opt(b, "periodMs", 800);
        Spec->blinkMin = opt(b, "min", 0.25f);
    }
    if (j.contains("bob") && j["bob"].is_object()) {
        const auto& b = j["bob"];
        Spec->hasBob = true;
        Spec->bobPeriodMs = opt(b, "periodMs", 900);
        Spec->bobDistance = opt(b, "distance", 4.0f);
    }
    if (j.contains("frames") && j["frames"].is_object()) {
        const auto& f = j["frames"];
        Spec->hasFrames = true;
        Spec->framesCount = std::max(1, opt(f, "count", 4));
        Spec->framesPeriodMs = std::max(1, opt(f, "periodMs", 600));
    }
    if (j.contains("exit") && j["exit"].is_object()) {
        const auto& e = j["exit"];
        Spec->exitType = opt(e, "type", std::string("fade"));
        Spec->exitDurationMs = std::max(1, opt(e, "durationMs", 180));
    }
    if (j.contains("tween") && j["tween"].is_object()) {
        Spec->hasTween = true;
        Spec->tweenDurationMs = std::max(1, opt(j["tween"], "durationMs", 300));
    }

    return Spec->any() ? Spec : nullptr;
}

}  // namespace kaedevn::ui
