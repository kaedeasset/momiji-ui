// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiResources.h — フォント（KaedeUiFonts.cpp）とブラシ（KaedeUiBrushes.cpp）の控えを捨てる口（モジュール内だけ）。
//
// 合成フォント・同梱フォントの置き場・角丸ブラシ・枠ブラシは使い回すために控えている。
// 以前は関数内 static で、モジュールの終了でも解放されなかった（レビュー 2026-10-17 R2-MOMIJI-06）。
// FMomijiUISlateModule::ShutdownModule がこの 2 つを呼ぶ。
//
// 2026-10-18 Issue #73
#pragma once

namespace KaedeUI
{
	void ReleaseKaedeUiFonts();
	void ReleaseKaedeUiBrushes();
}
