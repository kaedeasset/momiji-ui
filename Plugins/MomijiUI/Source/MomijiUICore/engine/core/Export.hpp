// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// MOMIJIUICORE_API の補い（engine/ の公開ヘッダはここを include する。1 か所だけに書く）。
// UE ビルドでは UBT が MOMIJIUICORE_API(=DLLEXPORT/DLLIMPORT) を定義する（別DLL＝MomijiUISlate・KaedeVnCore から呼ぶための export。
// 定石: memory ue-module-architecture）。ただし MomijiUICore の純C++ TU は UE ヘッダを include しないため
// DLLEXPORT 自体が未定義 → ここで補う。非UE（Tools/rpg-cli 等）では MOMIJIUICORE_API ごと空定義で素通し。
#ifndef MOMIJIUICORE_API
#define MOMIJIUICORE_API
#endif
#if !defined(DLLEXPORT) && defined(_MSC_VER)
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#endif
