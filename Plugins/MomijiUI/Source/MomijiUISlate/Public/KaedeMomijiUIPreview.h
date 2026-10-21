// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

#include "CoreMinimal.h"

// MomijiUI 画面プレビュー（PoC）— 任意の画面JSONファイルを UiNode→ComputeDrawList で描く。
// 本体は KaedeUI 内（UiNode/nlohmann を内包）。KaedeEditor は本ファクトリだけを呼ぶので
// nlohmann を引き込まない。ScreenPath/ThemePath は絶対パス（ThemePath は空可＝既定テーマ）。
class SWidget;

// RootDir: 画面一覧を集める置き場（絶対パス。その下の **/ui/*.json を一覧に出す）。MomijiUI は作品の置き場を
// 自分で持たないので、呼ぶ側が渡す（KaedeVN のエディタは KaedeVn::GetWorkRoot()）。
// ListFilter: 画面一覧に出す名前の絞り込み（部分一致・空なら RootDir/**/ui/*.json を全部出す）。
// 用途を絞ったタブ（攻略地図など）で、関係ない画面まで並ぶのを防ぐために使う。
// bShowStyleControls: テーマ(atelier/midnight/sakura)と端末(PC/タッチ)の切替を出すか。
// UI の見た目を作り込むための道具なので、中身を読むだけのタブでは邪魔になる。
// OnTitleChanged: 画面を切り替えるたびに、その画面JSON の "title" を渡す（無ければ空）。
// ドックタブのラベルをこれに追従させると、タブ名と画面の見出しが必ず同じ文字列になる。
MOMIJIUISLATE_API TSharedRef<SWidget> MakeKaedeMomijiUIPreview(const FString& ScreenPath, const FString& ThemePath,
                                                       const FString& RootDir,
                                                       const FString& ListFilter = FString(),
                                                       bool bShowStyleControls = true,
                                                       TFunction<void(const FString&)> OnTitleChanged = nullptr);
