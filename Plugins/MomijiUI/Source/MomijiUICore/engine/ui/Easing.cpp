// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// Easing.cpp — イージング11種の実装と、名前→enum の解決。
//
// 語彙もアルゴリズムも monorepo の packages/core/src/timeline/easing.ts が正で、
// **キー名は 1:1 で一致させる**。数値一致はテスト（MomijiUI.Core.Easing）で担保する。
// 実装を「それらしく」書き直すと web/native と挙動がズレるので、移植元を見て直すこと。
//
// UE 非依存（標準ライブラリのみ）。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "Easing.hpp"

#include <cmath>
#include <unordered_map>

namespace kaedevn::ui {

Easing easingFromString(const std::string& name, bool* ok)
{
    // easing.ts easingFunctions のキーと 1:1。
    static const std::unordered_map<std::string, Easing> kMap = {
        { "linear",         Easing::Linear },
        { "easeIn",         Easing::EaseIn },
        { "easeOut",        Easing::EaseOut },
        { "easeInOut",      Easing::EaseInOut },
        { "easeInQuad",     Easing::EaseInQuad },
        { "easeOutQuad",    Easing::EaseOutQuad },
        { "easeInOutQuad",  Easing::EaseInOutQuad },
        { "easeInCubic",    Easing::EaseInCubic },
        { "easeOutCubic",   Easing::EaseOutCubic },
        { "easeInOutCubic", Easing::EaseInOutCubic },
        { "step",           Easing::Step },
    };
    const auto It = kMap.find(name);
    if (ok) *ok = (It != kMap.end());
    return It != kMap.end() ? It->second : Easing::Linear;
}

double applyEasing(double t, Easing e)
{
    switch (e) {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
    case Easing::EaseInQuad:
        return t * t;
    case Easing::EaseOut:
    case Easing::EaseOutQuad:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case Easing::EaseInOut:
    case Easing::EaseInOutQuad:
        return t < 0.5 ? 2.0 * t * t
                       : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
    case Easing::EaseInCubic:
        return t * t * t;
    case Easing::EaseOutCubic:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case Easing::EaseInOutCubic:
        return t < 0.5 ? 4.0 * t * t * t
                       : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case Easing::Step:
        return t >= 1.0 ? 1.0 : 0.0;
    }
    return t;  // 到達しない（enum 網羅）が、コンパイラ警告避け
}

double applyEasingSafe(double t, Easing e)
{
    const double Clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return applyEasing(Clamped, e);
}

}  // namespace kaedevn::ui
