// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeTextSelection.h — 文字の選択の状態と規則（2026-10-07・設計 Docs/Reports/2026/10/07/01・仕様 momiji-ui-framework.md §7「選択」）。
//
// selectable の器の中の Label の行を集め、ドラッグで選んだ範囲（行, 文字番号）と写す文を決める。
// ブラウザ（Tools/momiji-ui/web/src/momiji-ui.mjs の selAreaAt / selPosAt / selText と attach の select）と同じ規則。
// 照合（Plugins/MomijiUI/Fixtures/select-text.json）が両方を同じ期待値と比べる。
// SKaedeUiCanvas が 1 つ持ち、押す・動かす・離す・描くはキャンバスの側（KaedeTextSelection.cpp の後半）が繋ぐ。
// 2026-10-18 に SKaedeUiCanvas から分けた（Issue #73）。
#pragma once

#include "CoreMinimal.h"
#include "SKaedeUiCanvas.h"

namespace KaedeUI
{
	// コードポイントの先頭の位置（UTF-16 の番号）と末尾（Len）。文字番号 k の位置は [k]、字数は Num()-1。
	// サロゲートペアは割らない（結合文字・異体字セレクタの保護はしない。設計 10/07/01 §4.2）。ブラウザの Array.from と同じ単位
	TArray<int32> CpStarts(const FString& S);

	// 選択が測るときと読むときにキャンバスから借りるもの。描くのと同じ書体・大きさで測る
	struct FKaedeSelEnv
	{
		int32 FontSize = KaedeLogicalFontSize;
		EKaedeFontFamily Family = EKaedeFontFamily::Gothic;
		float Scale = 1.f;                                              // 最後に描いた倍率（未描画は 1）
		TFunction<float(const FString& ScrollId)> ScrollOffset;         // クランプ済みのスクロール量
		TFunction<const FVector4f*(const FString& ScrollId)> Viewport;  // ScrollView の見える範囲 (x,y,w,h)。無ければ nullptr
	};

	struct FKaedeTextSelection
	{
		struct FLine
		{
			FString Group, Text, ScrollId;
			int32 Run = -1;
			float X = 0.f, Y = 0.f, LineH = 0.f, FontPt = 0.f;   // Y は ScrollView の中ならスクロール前の座標
			bool bBold = false;
		};
		struct FArea { FString Group, ScrollId; float X = 0.f, Y = 0.f, W = 0.f, H = 0.f; int32 FocusGroup = 0; };
		struct FPos { int32 Line = 0, Ch = 0; };   // Line は器の中の行の番号
		struct FState { FString Group; FPos Anchor, Focus; TArray<FString> Texts; int32 FirstLine = 0; };
		struct FPress { FString Group; FVector2f P = FVector2f::ZeroVector; bool bDragging = false; bool bDead = false; };

		// 行と器（SetContent が描画命令から集め直す）
		TArray<FLine> Lines;
		TArray<FArea> Areas;
		TMap<FString, TArray<int32>> GroupLines;   // 器の鍵 → Lines の番号（描画順）
		// 選んでいる範囲・押した点（4px 動くまでは選択ではない）・「コピーしました」の札（900ms）
		TOptional<FState> State;
		TOptional<FPress> Press;
		double CopiedAt = -1.0;
		FVector2f CopiedPos = FVector2f::ZeroVector;

		void ResetCollected();
		// 描画命令を 1 つ見る（SelectArea なら器、器の中の Text なら行）。ScrollId はその命令が入っている ScrollView
		void AddCmd(const FKaedeDrawCmd& C, const FString& ScrollId, int32 FontSize);
		// §4.4: 選んでいた行の文字が変わった・無くなったら選択を消す（消したら true。ドラッグ中なら離すまで何もしない）
		bool DropIfChanged();

		const FLine* LineOf(const FString& Group, int32 Line) const;
		float PrefixW(const FKaedeSelEnv& E, const FLine& Line, int32 Chars) const;   // 先頭から Chars 字の送り幅（論理px）
		int32 ChAt(const FKaedeSelEnv& E, const FLine& Line, float Dx) const;          // 行の X からのずれ → 文字番号
		FPos PosAt(const FKaedeSelEnv& E, const FString& Group, FVector2f L) const;    // §4.2
		FString TextOf(const FString& Group, FPos A, FPos B) const;                    // §4.5
		// 論理座標の点を含む器（最前面の群・見える範囲の中）。押せる当たりの上かどうかは呼び出し側が先に見る
		FString AreaAt(const FKaedeSelEnv& E, FVector2f L, int32 TopGroup) const;
		// 選べる行の字の上か（I ビーム）
		bool IsOverText(const FKaedeSelEnv& E, const FString& Group, FVector2f L) const;
		// 押した点から 4px 動いた: 選択を始める（押した点を anchor）
		void Begin(const FKaedeSelEnv& E);
		// いまの点まで伸ばし、範囲の行の文字を控える（§4.4）
		void ExtendTo(const FKaedeSelEnv& E, FVector2f L);
		// 選んでいる範囲を描画順に並べる（無ければ false）
		bool Ordered(FPos& OutA, FPos& OutB) const;
	};
}
