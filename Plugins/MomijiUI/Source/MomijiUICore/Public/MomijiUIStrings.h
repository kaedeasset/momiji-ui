// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"

#include <string>

// UE の文字列（FString・TCHAR）と純 C++ の層（std::string・UTF-8）の変換。
// 画面 JSON の木・ADV の core・放置錬金の core は UTF-8 の std::string で持つので、host が境で変える。
// **変換の定義はここ 1 か所**（2026-10-18 Issue #68: 7 ファイルに ToStd/ToFStr・Wide/Utf8・U8/FromU8 の 3 種類があった）。
namespace KaedeCore
{
	inline std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
	inline FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}
