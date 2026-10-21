// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// Easing — UIモーション/タイムラインのイージング核。
//
// 語彙の正は monorepo packages/core/src/timeline/easing.ts（11種）。名前・アルゴリズム
// とも 1:1 移植で、テストベクトル（EasingTests.cpp）で数値一致を検証する。
// web renderer の4種独自実装（easing.ts と二重化ドリフト中）の轍は踏まず、
// C++ はこのファイルだけを正とする。
//
// Interp.hpp/.cpp は「データバインド層」（{var}補間・showIf/hideIf）であり、
// イージングはここに置く（同名の罠に注意。設計 Docs/Reports/2026/07/21/01 §リスク）。
//
// UE 非依存（標準ライブラリのみ）。Tween.hpp（トラック/状態）・VNエフェクト
// （FKaedeOpHandler の SHAKE/FLASH 等）が共有する。

#include <string>

#include "../core/Export.hpp"   // MOMIJIUICORE_API

namespace kaedevn::ui {

// core easing.ts の Easing 型と 1:1（順序も types.ts の宣言順に合わせる）。
enum class Easing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    Step,
};

// 名前文字列（JSON の "easing" キー値）→ Easing。未知は Linear フォールバック
// （easing.ts applyEasing と同じ扱い）。ok を渡すと既知だったかを返す。
MOMIJIUICORE_API Easing easingFromString(const std::string& name, bool* ok = nullptr);

// t∈[0,1] → 進行度[0,1]。各関数は easing.ts と同一式。
MOMIJIUICORE_API double applyEasing(double t, Easing e);
// t を [0,1] にクランプしてから適用（easing.ts applyEasingSafe と同一）。
MOMIJIUICORE_API double applyEasingSafe(double t, Easing e);

}  // namespace kaedevn::ui
