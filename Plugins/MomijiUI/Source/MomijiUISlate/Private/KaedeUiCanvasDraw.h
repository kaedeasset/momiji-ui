// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiCanvasDraw.h — SKaedeUiCanvas::OnPaint の部品（モジュール内だけ）。
//
// OnPaint は「命令 1 つを合成して描く」を繰り返す。そのうちキャンバスの状態を読まない所をここへ出した:
//   描き先（FKaedePaintCtx）・重ね順の段（FKaedeLayerStack）・合成（押下の縮み・動き・色の混ぜ・焦点の見た目）・
//   Kind ごとの描き方・スクロールバー・焦点の枠。
// 押下/ホバー/動きの状態を読んで「どう合成するか」を決めるのはキャンバス（SKaedeUiCanvas.cpp の ComposeCmd）。
// 2026-10-18 に OnPaint（850 行）から分けた（Issue #73）。
#pragma once

#include "CoreMinimal.h"
#include "SKaedeUiCanvas.h"
#include "Rendering/DrawElements.h"

namespace kaedevn { namespace ui { struct UiMotionSample; } }

namespace KaedeUI
{
	// 1 フレームの描き先。論理座標 → ウィジェットのローカル座標はレターボックス（Scale・Off）
	struct FKaedePaintCtx
	{
		FKaedePaintCtx(const FGeometry& InGeo, FSlateWindowElementList& InOut, float InScale, float InOffX, float InOffY,
		               int32 InFontSize, EKaedeFontFamily InFamily);
		const FGeometry& Geo;
		FSlateWindowElementList& Out;
		float Scale, OffX, OffY;
		const FSlateBrush* White;
		int32 FontSize;
		EKaedeFontFamily Family;

		FVector2f ToLocal(float X, float Y) const { return FVector2f(OffX + X * Scale, OffY + Y * Scale); }
		// 論理の左上 (X,Y) に、描く大きさ (W,H)（px・Scale 済み）の PaintGeometry
		FPaintGeometry Box(float X, float Y, float W, float H) const
		{
			return Geo.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(ToLocal(X, Y)));
		}
		// 論理の矩形でクリップする。クリップ域は PaintGeometry（絶対トランスフォーム込み）で作る。FSlateRect 版は座標を
		// 絶対空間として扱うため、ウィジェットが原点でないと論理→ローカルのズレでクリップがずれる
		void PushClip(float X, float Y, float W, float H) const { Out.PushClip(FSlateClippingZone(Box(X, Y, W * Scale, H * Scale))); }
	};

	// 重ね順（2026-09-11・Docs/Reports/2026/08/29/04 §2.4）。矩形は Layer、文字・画像・線は Layer+1 の2段だけだと、
	// 後から出した矩形（Modal の暗幕・picker の板・ドロワー）が、先に出した画像や文字の**下**に沈む。
	// 提出順を守るため、「Layer+1 に置いたものと重なる矩形」が来たら段を 2 つ上げる。重ならない限り上げないので、
	// 一覧のように下へ積むだけの画面では段は増えない（Slate のバッチ数を増やさない）。上げたら重なりの記録は捨てる。
	// 矩形どうし（2026-09-12）: 同じ Layer でもブラシが違えば Slate は別バッチにし、バッチの順は提出順ではない
	//（白ブラシのバッチは画面の下地で先に作られるので、角丸の板の上に後から出した直角の矩形はそこへ合流して
	// 板の下に沈む。手帳の「新着」の 3px 線がこれで見えなかった）。ブラシが違う矩形と重なる矩形も段を上げる。
	struct FKaedeLayerStack
	{
		explicit FKaedeLayerStack(int32 Base) : Layer(Base) {}
		int32 Layer;
		// 命令 C を描く直前に呼ぶ（段を上げるか決め、C の範囲を記録する）
		void Before(const FKaedeDrawCmd& C, float Scale, int32 FontSize);
	private:
		struct FRectSince { int32 BrushKey = 0; float MinX = 0.f, MinY = 0.f, MaxX = 0.f, MaxY = 0.f; };
		bool bHiSince = false;
		float HiMinX = 0.f, HiMinY = 0.f, HiMaxX = 0.f, HiMaxY = 0.f;
		TArray<FRectSince, TInlineAllocator<4>> RectsSince;   // 段を最後に上げてから出した矩形の塗り（ブラシごとに範囲の和）
		void NoteHi(float X, float Y, float W, float H);
		void Raise();
	};

	// ---- 合成（命令の写しを書き換える）----
	void ShiftKaedeCmdY(FKaedeDrawCmd& C, float D);   // Lines（F-2）は座標を Points に持つので、Points も同じだけずらす
	void ScaleKaedeCmdAbout(FKaedeDrawCmd& C, float Px, float Py, float S);   // 点 (Px,Py) を中心に縮める（形は大きさも）
	void BlendKaedeCmdColor(FKaedeDrawCmd& C, float FbMul, float FbWhite);    // RGB に FbMul を掛け、FbWhite だけ白へ寄せる
	// UIモーションの sample を合成（offset → scale → alpha → countUp の文字 → 縦の開閉 → コマ → ゲージ）。ScrollOffset はこの命令の ScrollView のスクロール量
	void ApplyKaedeMotion(FKaedeDrawCmd& C, const kaedevn::ui::UiMotionSample& MS, float ScrollOffset);
	// 焦点の部品の見た目（テーマの select・2026-10-12）: 文字の色・最初の地の色を替える。地の無い項目（文字だけ）は
	// 当たりの矩形（FocusHit、HitY はスクロール後の上端）に地と枠を Layer に敷く。bFillDone を受け取り、地を替えたら true を返す
	bool ApplyKaedeFocusStyle(const SKaedeUiCanvas::FFocusStyle& S, FKaedeDrawCmd& C, bool bFillDone,
	                          const FKaedeHit* FocusHit, float HitY, const FKaedePaintCtx& P, int32 Layer);

	// ---- Kind ごとの描き方（合成の済んだ命令。Layer は段の基準＝矩形は Layer・文字と線は Layer+1）----
	void DrawKaedeRect(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedeRectOutline(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedeLines(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedeEllipse(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedeText(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	// Brush はキャンバスが解いた画像（無ければ沈み色の板）。ImgL は画像の明るさ（押下で沈む・ホバーで光る）
	void DrawKaedeImage(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C, const FSlateBrush* Brush, float ImgL);
	void DrawKaedeGradient(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedePolygon(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	void DrawKaedeBlur(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C);
	// 編集していない文字欄。Buffer は確定済みの文字（空なら placeholder の C.Text を薄色で）
	void DrawKaedeTextField(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& C, const FString& Buffer);

	// スクロールバー（中身がビューポートを超えるときだけ・ビューポート右端）。**寸法は KaedeUI::ScrollBarGeometry だけが持つ**
	//（掴む側＝OnMouseButtonDown と同じ値。ここに数式を戻さないこと・設計 09/20/05 §2.2）。Clip はその ScrollView の ClipPush
	void DrawKaedeScrollBar(const FKaedePaintCtx& P, int32 Layer, const FKaedeDrawCmd& Clip, float Offset, const FLinearColor& Thumb);
	// 焦点の枠（ゲームパッド/キーボード操作中のみ）: Cursor（テーマの select.cursor）があれば部品の左の外へ絵を置き、
	// 絵も文字・地の色替えも無いときだけ accent の枠
	void DrawKaedeFocusRing(const FKaedePaintCtx& P, int32 Layer, const FKaedeHit& H, const SKaedeUiCanvas::FFocusStyle& S, const FSlateBrush* Cursor);
}
