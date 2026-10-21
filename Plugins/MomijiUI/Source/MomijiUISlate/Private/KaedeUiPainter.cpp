// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// KaedeUiPainter.cpp — UiNode ツリー → 論理座標の描画コマンド列（ComputeDrawList）。
//
// **Slate を一切呼ばない。** 出すのは矩形・テキスト・画像の座標と色だけで、実際の
// 描画は SKaedeUiCanvas / SKaedeMomijiUIWidget が行う。この分離のおかげで、
// レイアウト幾何を GUI 無しで回帰検査できる（MomijiUI.* の契約テスト群）。
//
// 注意点:
// - **フォントpx を知らない。** `fontPt=0` は「ホスト既定」の意味で、計測にも描画にも
//   0 のまま渡して両者を一致させる。そのため折り返し行の送りは既定値 kLineH=30 に頼る。
//   フォントだけ大きくすると行が重なるので、JSON 側で `fontSize` と `lineH` を明示する
//   （2026-07-28 に 1080p で実際に重なった）
// - 行送りの比 kLineHRatio(30/22) は**放置錬金の wrapW ノードも共有**している。
//   ここを変えると Game/ui/alchemist/*.json のレイアウトが全部ずれる
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "KaedeUiPainter.h"
#include "MomijiUICore.h"   // KaedeCore::LayoutRuns（layoutRuns の MOMIJIUICORE_API ラッパ）

#include "core/UiAnim.hpp"       // UiAnimSpec（"anim" 語彙）
#include "ui/Tween.hpp"          // UiMotionState（安定キー登録）

#include <cstdlib>
#include <string>
#include <vector>

using kaedevn::ui::UiNode;
using kaedevn::ui::UiType;
using kaedevn::ui::UiTheme;
using kaedevn::ui::Color;
using kaedevn::ui::UiPoint;   // Polyline の頂点（F-2）

namespace
{
	FColor ToFColor(const Color& C) { return FColor(C.r, C.g, C.b, C.a); }

	std::string FontIdFor(const UiNode& N, const char* Fallback)
	{
		return N.font.empty() ? std::string(Fallback) : N.font;
	}
	bool IsBold(const std::string& FontId) { return FontId == "bold"; }

	struct FEmitCtx
	{
		TArray<KaedeUI::FKaedeDrawCmd>* Out = nullptr;
		const UiTheme* Theme = nullptr;
		const KaedeUI::FKaedeMeasureFn* Measure = nullptr;
		TArray<KaedeUI::FKaedeHit>* Hits = nullptr;  // 任意。Button のクリック領域収集用。
		int32 VpW = 0, VpH = 0;                      // Modal の暗幕/中央寄せに使う論理ビューポート。
		// アクティブなUIスキン（Component Standard のロール描画とpc98の直角化に使う）。
		KaedeUI::EKaedeUiSkinFamily SkinFamily = KaedeUI::EKaedeUiSkinFamily::PremiumAtelier;
		int32 FocusGroup = 0;   // 現在の Focus グループ（Modal 配下はその Modal の番号）。
		int32 ModalSeq = 0;     // 描いた Modal の通し番号＝群の番号。入れ子でも兄弟でも**後に描いた Modal が最前面**（仕様 §5.4・2026-10-04）
		FString ScrollId;       // ScrollView 配下を描画中なら非空（配下の hit に写す）。
		// ---- 文字の選択（2026-10-07・設計 10/07/01 §4.1）----
		FString SelGroup;       // いまの selectable の器の鍵（空=選べない）
		int32 SelRunSeq = 0;    // その器の中で次の Label に振る番号
		int32 SelOrd = 0;       // id の無い selectable の器に振る配置順の番号（"ord:<n>"）
		int32 SelRun = -1;      // >=0 の間、EmitLabel / EmitRuns が出す本文の行は選べる（Label の本文だけ。Button 等の文字は -1 のまま）

		// ---- UIモーション（設計 07/21/04。Motion==nullptr なら全て素通り）----
		kaedevn::ui::UiMotionState* Motion = nullptr;
		std::string MotionScreenId;                             // 安定キーの scope
		const TFunction<bool(const std::string&)>* MotionCond = nullptr;  // pulse whileIf
		std::string ActiveMotionKey;   // 適用中のキー（ネストは外側優先・内側の anim は無視）
		std::string MotionGroupKey;    // stagger の順位グループ＝直近の「名前を持つ」祖先のキー

		// ---- 2026-10-12（rpg-themes/README.md §3）----
		kaedevn::ui::UiTextOutline CurOutline;   // いまの文字の縁取り（部品 > テーマ）
		kaedevn::ui::UiTextGlow CurGlow;
		float CurSpacing = 0.f;
		bool CurMono = false;
		bool bHiText = false;                    // 選んでいる行の中（文字の色を替える）
		Color HiText{255, 255, 255, 255};
		// 半透明の窓どうしの重なり（theme.knockout）: 窓の地の描画命令と、その時点で開いていた窓
		struct FKnock { int32 CmdIdx; int x, y, w, h; int32 Owner; TArray<int32> Open; };
		TArray<FKnock> Knocks;
		TArray<int32> KnockOpen;   // いま開いている窓の通し番号（外から順）
		int32 KnockSeq = 0;
		// ---- 部品の配置の記録（2026-10-16・UI エディタ U3。OutRects。nullptr なら何もしない）----
		TArray<KaedeUI::FKaedeNodeRect>* Rects = nullptr;
		int32 RectDepth = 0;       // いま描いている部品の深さ（根が 0）
		bool bRectInRepeat = false; // repeat の複製の中
		FString RectTemplate;      // その複製の元の template の Path
	};

	// ヒット領域を1件登録する（Focus 可能領域も兼ねる。Phase 2-3）。
	// Src の action/id/focusable を写し、現在の FocusGroup を付与する。
	// bFocusable=false は Modal 背面吸収など「クリックは受けるが Focus 対象外」。
	void AddHit(FEmitCtx& Ctx, const UiNode& Src, float x, float y, float w, float h, bool bFocusable)
	{
		if (!Ctx.Hits) return;
		KaedeUI::FKaedeHit Hit;
		Hit.X = x; Hit.Y = y; Hit.W = w; Hit.H = h;
		Hit.Action = UTF8_TO_TCHAR(Src.action.c_str());
		Hit.bFocusable = bFocusable && Src.focusable;
		Hit.FocusId = UTF8_TO_TCHAR(Src.id.c_str());
		Hit.FocusGroup = Ctx.FocusGroup;
		Hit.ScrollId = Ctx.ScrollId;   // ScrollView 配下ならスクロールid を写す（Canvas が当たり判定で使用）
		Ctx.Hits->Add(Hit);
	}
	bool IsPc98(const FEmitCtx& Ctx) { return Ctx.SkinFamily == KaedeUI::EKaedeUiSkinFamily::Pc98; }
	// pc98 スキンでは既定角丸も直角にする（04 §基本トーン「角丸は禁止」）。
	float SkinRad(const FEmitCtx& Ctx, float Base) { return IsPc98(Ctx) ? 0.f : Base; }
	// 窓・押しボタンの角の丸み: テーマの radius があればそれ（2026-10-12・テーマの形）
	float BoxRad(const FEmitCtx& Ctx, float Base) { return IsPc98(Ctx) ? 0.f : (Ctx.Theme && Ctx.Theme->radius >= 0 ? float(Ctx.Theme->radius) : Base); }
	// この variant を pc98 規約（直角二重枠）で描くか。仕様09 §Phase3 の優先順位:
	//   pc98 スキン中は全要素を pc98 化 / atelier スキン中でも "pc98-" 明示だけはピン留めで pc98。
	bool RenderPc98(const FEmitCtx& Ctx, const std::string& v)
	{
		return IsPc98(Ctx) || v.rfind("pc98-", 0) == 0;
	}

	// 角丸の既定半径（見た目優先の UE 先行スタイル。0 を渡すと従来どおり直角）。
	// 参考: Docs/Reports/2026/07/11/02-idle-ui-quality-analysis-and-styling-spec.md §5-B
	constexpr float kRadPanel  = 8.f;
	constexpr float kRadButton = 10.f;
	constexpr float kRadModal  = 16.f;
	constexpr float kRadToggle = 6.f;
	// Component Standard（Codex 07/12 04/05/11 仕様）Phase 1: atelier-* の控えめ角丸。
	constexpr float kRadAtelier = 6.f;

	// 選択中(highlight)の背景明化: 塗り色を accent へ 18% 寄せる（「枠を濃く＋色を少し変える」
	// の統一表現 S2。設計 07/21/02）。枠の accent 化と常にセットで使う。
	Color HighlightFill(const Color& Base, const UiTheme& T)
	{
		auto Mix = [](uint8_t a, uint8_t b) { return (uint8_t)(a + (b - a) * 0.18f); };
		return Color{ Mix(Base.r, T.accent.r), Mix(Base.g, T.accent.g), Mix(Base.b, T.accent.b), Base.a };
	}

	void PushRect(FEmitCtx& Ctx, int x, int y, int w, int h, const Color& c, float Radius = 0.f)
	{
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Rect;
		Cmd.X = (float)x; Cmd.Y = (float)y; Cmd.W = (float)w; Cmd.H = (float)h;
		Cmd.Color = ToFColor(c);
		Cmd.Radius = Radius;
		Ctx.Out->Add(Cmd);
	}
	void PushOutline(FEmitCtx& Ctx, int x, int y, int w, int h, const Color& c, float Radius = 0.f, float Thick = 1.f)
	{
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::RectOutline;
		Cmd.X = (float)x; Cmd.Y = (float)y; Cmd.W = (float)w; Cmd.H = (float)h;
		Cmd.Color = ToFColor(c);
		Cmd.Radius = Radius;
		Cmd.Thickness = Thick;
		Ctx.Out->Add(Cmd);
	}

	// ---- 2026-10-12 の描き方の補助（rpg-themes/README.md §3）----
	// 濃淡の止め色から、位置 t（0〜1）の色
	Color GradAt(const kaedevn::ui::UiGradient& G, float t)
	{
		if (G.stops.empty()) return Color{0, 0, 0, 0};
		if (t <= G.stops.front().second) return G.stops.front().first;
		for (size_t i = 1; i < G.stops.size(); ++i)
		{
			const auto& A = G.stops[i - 1];
			const auto& B = G.stops[i];
			if (t <= B.second)
			{
				const float u = B.second > A.second ? (t - A.second) / (B.second - A.second) : 0.f;
				auto L = [u](uint8_t a, uint8_t b) { return (uint8_t)FMath::Clamp(FMath::RoundToInt(a + (b - a) * u), 0, 255); };
				return Color{L(A.first.r, B.first.r), L(A.first.g, B.first.g), L(A.first.b, B.first.b), L(A.first.a, B.first.a)};
			}
		}
		return G.stops.back().first;
	}
	Color MulA(Color c, float m) { c.a = (uint8_t)FMath::Clamp(FMath::RoundToInt(c.a * m), 0, 255); return c; }

	void PushGradient(FEmitCtx& Ctx, int x, int y, int w, int h, const kaedevn::ui::UiGradient& G, float Radius, float AlphaMul = 1.f)
	{
		if (w <= 0 || h <= 0 || !G.on) return;
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Gradient;
		Cmd.X = (float)x; Cmd.Y = (float)y; Cmd.W = (float)w; Cmd.H = (float)h;
		Cmd.Radius = Radius;
		Cmd.bGradH = G.horizontal;
		for (const auto& St : G.stops) { Cmd.GradColors.Add(ToFColor(MulA(St.first, AlphaMul))); Cmd.GradPos.Add(St.second); }
		Cmd.Color = FColor::White;   // 色は GradColors。動き・薄くするは Alpha と GradColors に掛ける
		Ctx.Out->Add(Cmd);
	}

	// 箱の形（hex＝両端が尖る・tagL／tagR＝片方が尖る・slant＝平行四辺形）の頂点
	TArray<FVector2f> ShapePoints(const std::string& Shape, int x, int y, int w, int h, int InsetIn)
	{
		const float X = (float)x, Y = (float)y, W = (float)w, H = (float)h;
		const float I = FMath::Min(InsetIn >= 0 ? (float)InsetIn : H * 0.35f, W * 0.45f);
		if (Shape == "hex") return {{X + I, Y}, {X + W - I, Y}, {X + W, Y + H * 0.5f}, {X + W - I, Y + H}, {X + I, Y + H}, {X, Y + H * 0.5f}};
		if (Shape == "tagL") return {{X + I, Y}, {X + W, Y}, {X + W, Y + H}, {X + I, Y + H}, {X, Y + H * 0.5f}};
		if (Shape == "tagR") return {{X, Y}, {X + W - I, Y}, {X + W, Y + H * 0.5f}, {X + W - I, Y + H}, {X, Y + H}};
		if (Shape == "slant") return {{X + I, Y}, {X + W, Y}, {X + W - I, Y + H}, {X, Y + H}};
		return {{X, Y}, {X + W, Y}, {X + W, Y + H}, {X, Y + H}};
	}
	bool IsShaped(const std::string& Shape) { return !Shape.empty() && Shape != "rect"; }

	// 塗りの多角形。G があれば頂点ごとに濃淡の色（外接矩形の上→下か左→右）
	void PushPolygon(FEmitCtx& Ctx, const TArray<FVector2f>& Pts, const Color& C, const kaedevn::ui::UiGradient* G = nullptr, float AlphaMul = 1.f)
	{
		if (Pts.Num() < 3) return;
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Polygon;
		Cmd.Points = Pts;
		Cmd.Color = ToFColor(MulA(C, AlphaMul));
		float MinX = Pts[0].X, MaxX = Pts[0].X, MinY = Pts[0].Y, MaxY = Pts[0].Y;
		for (const FVector2f& P : Pts) { MinX = FMath::Min(MinX, P.X); MaxX = FMath::Max(MaxX, P.X); MinY = FMath::Min(MinY, P.Y); MaxY = FMath::Max(MaxY, P.Y); }
		Cmd.X = MinX; Cmd.Y = MinY; Cmd.W = MaxX - MinX; Cmd.H = MaxY - MinY;
		if (G && G->on)
			for (const FVector2f& P : Pts)
			{
				const float t = G->horizontal ? (Cmd.W > 0.f ? (P.X - MinX) / Cmd.W : 0.f) : (Cmd.H > 0.f ? (P.Y - MinY) / Cmd.H : 0.f);
				Cmd.PointColors.Add(ToFColor(MulA(GradAt(*G, t), AlphaMul)));
			}
		Ctx.Out->Add(Cmd);
	}
	void PushPolyOutline(FEmitCtx& Ctx, const TArray<FVector2f>& Pts, const Color& C, float Width)
	{
		if (Pts.Num() < 2 || Width <= 0.f) return;
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Lines;
		Cmd.Color = ToFColor(C);
		Cmd.Thickness = Width;
		Cmd.Points = Pts;
		const FVector2f First = Pts[0];
		Cmd.Points.Add(First);
		Ctx.Out->Add(Cmd);
	}
	// 絵（9 分割・敷き詰め・切り出し）
	void PushSprite(FEmitCtx& Ctx, const std::string& Sprite, float x, float y, float w, float h, const int* Slice, bool bTile, const int* Src, float Alpha = 1.f, const char* Fit = "stretch")
	{
		if (Sprite.empty() || w <= 0.f || h <= 0.f) return;
		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Image;
		Cmd.X = x; Cmd.Y = y; Cmd.W = w; Cmd.H = h;
		Cmd.Sprite = UTF8_TO_TCHAR(Sprite.c_str());
		Cmd.Fit = UTF8_TO_TCHAR(Fit);
		Cmd.Alpha = Alpha;
		if (Slice) for (int i = 0; i < 4; ++i) Cmd.Slice[i] = (float)Slice[i];
		Cmd.bTile = bTile;
		if (Src) for (int i = 0; i < 4; ++i) Cmd.Src[i] = (float)Src[i];
		Ctx.Out->Add(Cmd);
	}
	// 後ろの柔らかい光（外ほど薄い角丸の重ね）
	void PushGlow(FEmitCtx& Ctx, int x, int y, int w, int h, const kaedevn::ui::UiGlow& G)
	{
		if (!G.on || w <= 0 || h <= 0) return;
		const int L = 6;
		for (int k = L; k >= 1; --k)
		{
			const int E = G.size * k / L;
			const Color C = MulA(G.color, 1.f / L * 1.6f);
			PushRect(Ctx, x - E, y - E, w + 2 * E, h + 2 * E, C, FMath::Min(w, h) * 0.5f + E);
		}
	}
	// 立体の縁（上・左が light、下・右が dark）。o は外からの距離
	void PushEdge3d(FEmitCtx& Ctx, int x, int y, int w, int h, const kaedevn::ui::UiEdge3d& E, int o)
	{
		if (!E.on) return;
		const int W = E.width;
		if (w - 2 * o <= 2 * W || h - 2 * o <= 2 * W) return;
		PushRect(Ctx, x + o, y + o, w - 2 * o, W, E.light, 0.f);
		PushRect(Ctx, x + o, y + o, W, h - 2 * o, E.light, 0.f);
		PushRect(Ctx, x + o, y + h - o - W, w - 2 * o, W, E.dark, 0.f);
		PushRect(Ctx, x + w - o - W, y + o, W, h - 2 * o, E.dark, 0.f);
	}

	// 窓の枠（テーマの border・frame）。bDecor は窓（dark・light の入れ物と Modal）だけ: 二重線と四隅の飾り
	// Width は部品の borderWidth（-1 はテーマの border・0 は枠も飾りも描かない）
	void PushBoxFrame(FEmitCtx& Ctx, int x, int y, int w, int h, const Color& Edge, float Rad, bool bDecor, int Width = -1)
	{
		const UiTheme& T = *Ctx.Theme;
		if (Width == 0) return;
		const int B = Width > 0 ? Width : FMath::Max(1, T.border);
		// 太い枠は 1 本の太い線（角丸もきれいに・2026-10-12 B1）
		if (w > 0 && h > 0) PushOutline(Ctx, x, y, w, h, Edge, Rad, (float)B);
		if (!bDecor || IsPc98(Ctx)) return;
		if (T.frame == "double" || T.frame == "ornate")
		{
			const int In = B + 3;
			if (w > 2 * In && h > 2 * In) PushOutline(Ctx, x + In, y + In, w - 2 * In, h - 2 * In, Edge, FMath::Max(0.f, Rad - In));
		}
		if (T.frame == "ornate" && w > 24 && h >= 48)   // 細い帯（見出し・名前の札）には飾りを付けない
		{
			// 四隅の飾り: accent の角札に、地の色の芯
			const int S = 10, C = 4;
			for (const auto& P : {FIntPoint(x, y), FIntPoint(x + w, y), FIntPoint(x, y + h), FIntPoint(x + w, y + h)})
			{
				PushRect(Ctx, P.X - S / 2, P.Y - S / 2, S, S, T.accent, 0.f);
				PushRect(Ctx, P.X - C / 2, P.Y - C / 2, C, C, T.track, 0.f);
			}
		}
	}

	// 窓を 1 つ描く（2026-10-12・テーマの影・濃淡・上端の線・選択の帯）。bDecor は窓（dark・light の入れ物と Modal）だけ
	// N があれば、その部品の borderColor・borderWidth で枠を描く（選んでいる間の枠は accent のまま）
	// 2026-10-12: 部品とテーマの gradient・shape・edge3d・innerBorder・frameImage・pattern・glow、選んでいる行（theme.select）
	void PaintBox(FEmitCtx& Ctx, int x, int y, int w, int h, const Color& Fill, const Color& EdgeIn, float Rad, bool bDecor, bool bHighlight,
	              const UiNode* N = nullptr)
	{
		const UiTheme& T = *Ctx.Theme;
		const bool bSkin = bDecor && !IsPc98(Ctx);
		const bool bRow = bHighlight && !bDecor && T.select.on;   // 選んでいる行（窓そのものではない）
		Color Edge = (N && N->hasBorderColor && !bHighlight) ? N->borderColor : EdgeIn;
		if (bRow && T.select.hasBorder) Edge = T.select.border;
		const int Width = N ? N->borderWidth : -1;
		const std::string Shape = (N && !N->shape.empty()) ? N->shape : (bSkin ? T.windowShape : std::string());
		const bool bShaped = IsShaped(Shape);

		// 光（部品・選んでいる行）
		if (N && N->glow.on) PushGlow(Ctx, x, y, w, h, N->glow);
		else if (bRow && T.select.glow.on) PushGlow(Ctx, x, y, w, h, T.select.glow);

		if (bSkin && T.shadowColor.a > 0)
		{
			// ぼかしは重ねた矩形で近づける（外ほど薄い。重なりで中心が shadowColor.a に近づく）
			const int L = FMath::Clamp(T.shadowBlur / 2, 1, 8);
			const uint8_t A = (uint8_t)FMath::Max(1, T.shadowColor.a / L);
			for (int k = L - 1; k >= 0; --k)
			{
				const int G = k * 2;
				PushRect(Ctx, x + T.shadowX - G, y + T.shadowY - G, w + 2 * G, h + 2 * G,
				         Color{T.shadowColor.r, T.shadowColor.g, T.shadowColor.b, A}, Rad + G);
			}
		}

		// 地
		const kaedevn::ui::UiGradient* Gr = nullptr;
		if (N && N->gradient.on) Gr = &N->gradient;
		else if (bRow && T.select.gradient.on) Gr = &T.select.gradient;
		else if (bSkin && T.windowGradient.on) Gr = &T.windowGradient;
		const Color FillC = (bRow && T.select.hasFill) ? T.select.fill : Fill;
		// 窓の濃淡は、host が地の色に掛けた不透明度（設定の窓の濃さ）に従う
		const float AlphaMul = (Gr && Gr == &T.windowGradient && N && N->hasColor) ? N->color.a / 255.f : 1.f;
		TArray<FVector2f> Pts;
		if (bShaped)
		{
			Pts = ShapePoints(Shape, x, y, w, h, N ? N->shapeInset : -1);
			PushPolygon(Ctx, Pts, FillC, Gr, AlphaMul);
		}
		else if (Gr) PushGradient(Ctx, x, y, w, h, *Gr, Rad, AlphaMul);
		else
		{
			if (bSkin && T.knockout && FillC.a < 255)
			{
				FEmitCtx::FKnock K;
				K.CmdIdx = Ctx.Out->Num(); K.x = x; K.y = y; K.w = w; K.h = h;
				K.Owner = Ctx.KnockOpen.Num() ? Ctx.KnockOpen.Last() : -1;
				K.Open = Ctx.KnockOpen;
				Ctx.Knocks.Add(K);
			}
			PushRect(Ctx, x, y, w, h, FillC, Rad);
		}
		const int B = Width >= 0 ? Width : FMath::Max(1, T.border);
		// 模様（窓だけ・四角いとき）
		if (bSkin && !bShaped && !T.pattern.sprite.empty())
			PushSprite(Ctx, T.pattern.sprite, (float)(x + B), (float)(y + B), (float)(w - 2 * B), (float)(h - 2 * B), nullptr, true, nullptr, T.pattern.opacity);
		if (!bShaped && bSkin && T.gradient.a > 0)
		{
			// 縦の濃淡（2026-10-12 前半の形・1 色を下へ重ねる）
			const int R = FMath::CeilToInt(Rad);
			const int Y0 = y + FMath::Max(B, R), Y1 = y + h - FMath::Max(B, R);
			const int NStrip = 24;
			for (int k = 0; k < NStrip && Y1 > Y0; ++k)
			{
				const int a0 = Y0 + (Y1 - Y0) * k / NStrip, a1 = Y0 + (Y1 - Y0) * (k + 1) / NStrip;
				const uint8_t A = (uint8_t)(T.gradient.a * (k + 0.5f) / NStrip);
				if (a1 > a0) PushRect(Ctx, x + B, a0, w - 2 * B, a1 - a0, Color{T.gradient.r, T.gradient.g, T.gradient.b, A}, 0.f);
			}
		}
		if (!bShaped && bSkin && T.bevel.a > 0)
		{
			const int In = FMath::Max(B, FMath::CeilToInt(Rad * 0.6f));
			if (w > 2 * In) PushRect(Ctx, x + In, y + B, w - 2 * In, 1, T.bevel, 0.f);
		}

		// 枠
		if (bShaped)
		{
			if (Width != 0) PushPolyOutline(Ctx, Pts, Edge, (float)B);
		}
		else
		{
			const bool bFrameImg = bSkin && !T.frameImage.sprite.empty() && Width != 0;
			if (!bFrameImg || T.frameImage.over) PushBoxFrame(Ctx, x, y, w, h, Edge, Rad, bDecor, Width);
			if (bFrameImg && Width != 0)
				PushSprite(Ctx, T.frameImage.sprite, (float)x, (float)y, (float)w, (float)h, T.frameImage.slice, false, nullptr);
			// borderWidth 0 は枠を描かない＝テーマの立体の縁・内側の線・枠の絵も描かない（部品に直に書いたものは描く）
			const kaedevn::ui::UiEdge3d& E3 = (N && N->edge3d.on) ? N->edge3d : (bSkin && Width != 0 ? T.edge3d : kaedevn::ui::UiEdge3d{});
			PushEdge3d(Ctx, x, y, w, h, E3, Width == 0 ? 0 : B);
			const kaedevn::ui::UiInnerBorder& IB = (N && N->innerBorder.on) ? N->innerBorder : (bSkin && Width != 0 ? T.innerBorder : kaedevn::ui::UiInnerBorder{});
			if (IB.on)
			{
				const int In = IB.inset + (E3.on ? E3.width : 0);
				for (int i = 0; i < IB.width && w - 2 * (In + i) > 0 && h - 2 * (In + i) > 0; ++i)
					PushOutline(Ctx, x + In + i, y + In + i, w - 2 * (In + i), h - 2 * (In + i), IB.color, FMath::Max(0.f, Rad - In - i));
			}
		}
		// 選択の帯は行の左の外（窓の内側の余白）に置く。行の中に置くと文字の頭にかかる
		if (bHighlight && !bDecor && T.cursorBar > 0 && h > 8)   // 窓そのもの（見出しの帯など）には付けない
			PushRect(Ctx, x - T.cursorBar - 3, y + 4, T.cursorBar, h - 8, T.accent, T.cursorBar * 0.5f);
		// 選んでいる行のカーソルの絵（行の左端から dx・dy ずらした所）
		if (bRow && !T.select.cursor.sprite.empty())
		{
			const auto& Cu = T.select.cursor;
			const float cw = (float)(Cu.w > 0 ? Cu.w : 32), ch = (float)(Cu.h > 0 ? Cu.h : 24);
			PushSprite(Ctx, Cu.sprite, (float)(x + Cu.dx), (float)y + (h - ch) * 0.5f + Cu.dy, cw, ch, nullptr, false, nullptr, 1.f, "contain");
		}
	}

	// ===== Component Standard（Codex 07/12 04/05/09/11）=====
	// コンテナ（Panel / V・H Layout）の variant をロールとして解決し、アクティブな
	// スキンファミリの部品規約で描く。テーマ11ロールのみ使用（hex 直書き不要）。
	//   PremiumAtelier … 控えめ角丸 6px ＋ 金枠 1本
	//   Pc98           … 直角（Radius 0）＋二重枠（外=panelEdge / 内=accent系を2pxインセット）
	// atelier-*/pc98-* はロール別名（surface/card/tile）。ファミリ横断で自動変換する
	// （スキン一括切替のため。09 §Phase3 の「JSON明示優先」はファミリ内指定として解釈）。
	// pc98 ファミリでは旧 variant light/dark もロール（card/surface）として扱う。
	// 非該当 variant は false のまま既存パスに流す（非破壊）。
	enum class EContainerRole : uint8 { None, Surface, Card, Tile };
	EContainerRole ContainerRoleFor(const std::string& v, const FEmitCtx& Ctx)
	{
		if (v == "atelier-surface")   return EContainerRole::Surface;
		if (v == "atelier-card")      return EContainerRole::Card;
		if (v == "atelier-icon-tile") return EContainerRole::Tile;
		if (v == "pc98-dark")         return EContainerRole::Surface;
		if (v == "pc98-row")          return EContainerRole::Card;
		if (v == "pc98-tile")         return EContainerRole::Tile;
		if (IsPc98(Ctx))
		{
			if (v == "dark")  return EContainerRole::Surface;
			if (v == "light") return EContainerRole::Card;
		}
		return EContainerRole::None;
	}

	bool EmitContainerStandard(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		const EContainerRole Role = ContainerRoleFor(n.variant, Ctx);
		if (Role == EContainerRole::None) return false;

		if (RenderPc98(Ctx, n.variant))
		{
			// pc98: surface=黒窓(track) / card=リスト行(panel) / tile=黒タイル(track+内側accent)。
			const Color& Fill = Role == EContainerRole::Card ? T.panel : T.track;
			const Color& Inner = Role == EContainerRole::Tile ? T.accent : T.accentDim;
			PushRect(Ctx, n.x, n.y, n.w, n.h, Fill, 0.f);
			PushOutline(Ctx, n.x, n.y, n.w, n.h, T.panelEdge, 0.f);
			PushOutline(Ctx, n.x + 2, n.y + 2, n.w - 4, n.h - 4, Inner, 0.f);
			return true;
		}
		// premium-atelier: surface=panel / card=panelLight / tile=track。金枠1本＋角丸6。
		// highlight（選択中 S2・07/21/02）は accent 枠に加えて背景も accent 側へ寄せる
		//（枠だけでは一覧で追いにくい。「枠を濃く＋色を少し変える」の統一表現）。
		const Color& Base = Role == EContainerRole::Card ? T.panelLight
		                  : Role == EContainerRole::Tile ? T.track : T.panel;
		const Color Fill = n.highlight ? HighlightFill(Base, T) : Base;
		PaintBox(Ctx, n.x, n.y, n.w, n.h, Fill, n.highlight ? T.accent : T.panelEdge, BoxRad(Ctx, kRadAtelier),
			         true, n.highlight, &n);
		return true;
	}

	// Label の描画（実装は下方。Component Standard の Button から使う）。
	// lineHOverride>0 は wrapW 複数行の行送りを明示指定（0=fontPt からの比で算出）。
	// 文字を積む（テーマの textShadow があれば、先にずらした影を積む。影は選択の対象にしない）
	void AddText(FEmitCtx& Ctx, const KaedeUI::FKaedeDrawCmd& CmdIn)
	{
		const UiTheme& T = *Ctx.Theme;
		KaedeUI::FKaedeDrawCmd Cmd = CmdIn;
		if (Ctx.bHiText) Cmd.Color = FColor(Ctx.HiText.r, Ctx.HiText.g, Ctx.HiText.b, Cmd.Color.A);
		Cmd.LetterSpacing = Ctx.CurSpacing;
		// 光（縁取りを太くした薄い写しを 2 枚、先に積む。選択の対象にしない）
		if (Ctx.CurGlow.on && !IsPc98(Ctx))
		{
			// 縁取りを太くした薄い写しを 1 枚（柔らかい影の代わり。Slate の文字はぼかせない）
			KaedeUI::FKaedeDrawCmd Gl = Cmd;
			const Color& GC = Ctx.CurGlow.color;
			const uint8 A = (uint8)FMath::Clamp((int)GC.a * Cmd.Color.A / 255 * 2 / 5, 0, 255);
			Gl.Color = FColor(GC.r, GC.g, GC.b, A);
			Gl.OutlinePx = FMath::Max(1.f, (float)Ctx.CurGlow.size * 0.5f);
			Gl.OutlineColor = FColor(GC.r, GC.g, GC.b, A);
			Gl.SelGroup.Reset(); Gl.SelRun = -1;
			Ctx.Out->Add(Gl);
		}
		if (Ctx.CurOutline.on && !IsPc98(Ctx))
		{
			Cmd.OutlinePx = (float)Ctx.CurOutline.width;
			const Color& OC = Ctx.CurOutline.color;
			Cmd.OutlineColor = FColor(OC.r, OC.g, OC.b, (uint8)((int)OC.a * Cmd.Color.A / 255));
		}
		if (T.textShadow.a > 0 && !IsPc98(Ctx))
		{
			KaedeUI::FKaedeDrawCmd Sh = Cmd;
			Sh.X += T.textShadowX; Sh.Y += T.textShadowY;
			Sh.Color = FColor(T.textShadow.r, T.textShadow.g, T.textShadow.b, (uint8)((int)T.textShadow.a * Cmd.Color.A / 255));
			Sh.SelGroup.Reset(); Sh.SelRun = -1;
			Ctx.Out->Add(Sh);
		}
		Ctx.Out->Add(Cmd);
	}

	void EmitLabel(FEmitCtx& Ctx, const std::string& s, int x, int y, int w, int h,
	               const Color& c, const std::string& align, bool centerFallback,
	               const std::string& fontId, int wrapW, int fontPt = 0, int lineHOverride = 0,
	               bool bEllipsis = false);

	// Component Standard の Button variant（pc98-primary/ghost・atelier-primary/ghost）。
	// ロール（primary/ghost）をアクティブなファミリの規約で描く。pc98 スキンでは
	// 旧 primary/ghost（無指定含む）もロールとして取り込み、画面全体を直角規約にする。
	// atelier スキンでは旧 variant は従来パス（半径10）のまま＝既定の見た目を変えない。
	// 対応したら描画＋ヒット登録まで行い true。
	bool EmitButtonStandard(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		const std::string& v = n.variant;
		bool bPrimary = v == "pc98-primary" || v == "atelier-primary";
		bool bGhost   = v == "pc98-ghost" || v == "atelier-ghost";
		if (!bPrimary && !bGhost)
		{
			if (!IsPc98(Ctx)) return false;      // atelier: 旧 variant は従来パス
			bGhost = v == "ghost";               // pc98: 旧 variant もロールに変換
			bPrimary = !bGhost;
		}
		const std::string lbl = n.icon.empty() ? n.text : (n.icon + " " + n.text);

		if (RenderPc98(Ctx, v))
		{
			if (bPrimary)
			{
				// 実行: 濃色地＋強い（二重）枠＋淡色文字。
				PushRect(Ctx, n.x, n.y, n.w, n.h,
				         n.hasColor ? n.color : (n.enabled ? T.accentDim : T.panel), 0.f);
				PushOutline(Ctx, n.x, n.y, n.w, n.h, n.enabled ? T.accent : T.panelEdge, 0.f);
				PushOutline(Ctx, n.x + 2, n.y + 2, n.w - 4, n.h - 4,
				            n.enabled ? T.accent : T.panelEdge, 0.f);
			}
			else
			{
				// 補助/戻る: 弱い地＋細枠。
				PushRect(Ctx, n.x, n.y, n.w, n.h, n.hasColor ? n.color : T.track, 0.f);
				PushOutline(Ctx, n.x, n.y, n.w, n.h, T.panelEdge, 0.f);
			}
			EmitLabel(Ctx, lbl, n.x, n.y, n.w, n.h,
			            n.hasTextColor ? n.textColor : (n.enabled ? T.textPrimary : T.textDim),
			            "center", true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
		}
		else  // atelier
		{
			if (bPrimary)
			{
				// 最重要操作: 金地＋暗文字（角丸6）。
				PushRect(Ctx, n.x, n.y, n.w, n.h,
				         n.hasColor ? n.color : (n.enabled ? T.accent : T.accentDim), kRadAtelier);
				EmitLabel(Ctx, lbl, n.x, n.y, n.w, n.h,
				            n.hasTextColor ? n.textColor : (n.enabled ? T.textOnAccent : T.textDim),
				            "center", true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
			}
			else
			{
				// 補助: 薄地＋金枠のみ（角丸6）。
				PushRect(Ctx, n.x, n.y, n.w, n.h, T.panel, kRadAtelier);
				PushOutline(Ctx, n.x, n.y, n.w, n.h,
				            n.highlight ? T.accent : T.panelEdge, kRadAtelier);
				EmitLabel(Ctx, lbl, n.x, n.y, n.w, n.h,
				            n.hasTextColor ? n.textColor : (n.enabled ? T.textPrimary : T.textDim),
				            "center", true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
			}
		}

		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		return true;
	}

	// 論理行送り。fontSize 無指定時の既定。
	constexpr int kLineH = 30;
	// 行送り / フォントpx の比。kLineH=30 は既定 22px 時の実測値なので 30/22。
	// fontSize 指定時はこの比で送りを決める（painter はホストの既定 px を知らないため、
	// 「既定サイズ基準の相対」ではなく比で決める）。
	constexpr float kLineHRatio = 30.f / 22.f;

	// fontSize 指定時の行送り。未指定(0)は従来どおり kLineH（幾何を一切変えない）。
	inline int LineHFor(int fontPt)
	{
		if (fontPt <= 0) return kLineH;
		return FMath::Max(1, FMath::RoundToInt((float)fontPt * kLineHRatio));
	}

	// 貪欲折り返し: 空白があれば単語境界で、無ければ（CJK/長語）文字境界で改行する。
	// measure は論理 px 幅を返すコールバック。'\n' は明示改行。
	TArray<FString> WrapToLines(const FString& Text, int wrapW, bool bBold, float fontPt,
	                            const KaedeUI::FKaedeMeasureFn& Measure)
	{
		TArray<FString> Lines;
		FString Cur;
		int32 LastBreak = -1;  // Cur 内で「この位置の前で改行可」(空白の直後)
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR Ch = Text[i];
			if (Ch == TEXT('\n')) { Lines.Add(Cur); Cur.Empty(); LastBreak = -1; continue; }
			Cur.AppendChar(Ch);
			if (Ch == TEXT(' ')) LastBreak = Cur.Len();
			if (Cur.Len() > 1 && Measure(Cur, bBold, fontPt) > (float)wrapW)
			{
				if (LastBreak > 0 && LastBreak < Cur.Len())
				{
					FString Line = Cur.Left(LastBreak); Line.TrimEndInline();  // 単語境界
					Cur = Cur.Mid(LastBreak);
					Lines.Add(Line);
				}
				else
				{
					const TCHAR Last = Cur[Cur.Len() - 1];               // 文字境界(CJK/長語)
					Cur.LeftChopInline(1);
					Lines.Add(Cur);
					Cur.Reset(); Cur.AppendChar(Last);
				}
				LastBreak = -1;
			}
		}
		if (Cur.Len() > 0) Lines.Add(Cur);
		return Lines;
	}

	// Label の描画（非 runs パス）。
	// 実測で箱(MaxW)に収まるところまで詰めて「…」を付ける。
	// 幅を測れるのは描画側だけなので、切る責任もここに置く（JSON を作る側の推定に頼らない）。
	// text を数として読む（読み方はここ 1 か所）。bWhole: 桁区切りの "," を剥いで全文が数のときだけ読む（countUp の "1,234"）。
	// bWhole でないとき: 先頭の数を読み、後ろの文字は見ない（Slider の "{item.hpRate}"）。bOutGrouped は "," があったか（bWhole のときだけ）。
	bool ReadTextNumber(const std::string& Text, bool bWhole, double& Out, bool* bOutGrouped = nullptr)
	{
		std::string Stripped;
		if (bWhole)
		{
			Stripped.reserve(Text.size());
			for (const char ch : Text)
			{
				if (ch == ',') { if (bOutGrouped) *bOutGrouped = true; continue; }
				Stripped += ch;
			}
		}
		const std::string& Src = bWhole ? Stripped : Text;
		char* End = nullptr;
		const double D = std::strtod(Src.c_str(), &End);
		if (!End || End == Src.c_str()) return false;
		if (bWhole && *End != '\0') return false;
		Out = D;
		return true;
	}

	FString EllipsizeToWidth(const KaedeUI::FKaedeMeasureFn* Measure, const FString& In,
	                         float MaxW, bool bBold, float EffPx)
	{
		if (!Measure || MaxW <= 0.f || In.IsEmpty()) return In;
		if ((*Measure)(In, bBold, EffPx) <= MaxW) return In;
		static const FString Dots(TEXT("…"));
		FString Out;
		for (int32 i = 0; i < In.Len(); )
		{
			// サロゲートペアは割らない（絵文字などで壊れた文字が出るのを防ぐ）
			const int32 Step = (In[i] >= 0xD800 && In[i] <= 0xDBFF && i + 1 < In.Len()) ? 2 : 1;
			const FString Next = Out + In.Mid(i, Step);
			if ((*Measure)(Next + Dots, bBold, EffPx) > MaxW) break;
			Out = Next;
			i += Step;
		}
		return Out.IsEmpty() ? Dots : Out + Dots;
	}

	void EmitLabel(FEmitCtx& Ctx, const std::string& s, int x, int y, int w, int h,
	               const Color& c, const std::string& align, bool centerFallback,
	               const std::string& fontId, int wrapW, int fontPt, int lineHOverride,
	               bool bEllipsis)
	{
		if (s.empty()) return;
		FString Text = UTF8_TO_TCHAR(s.c_str());
		const bool bBold = IsBold(fontId);
		// fontPt=0 は「ホストの既定」。0 のまま計測にも描画にも同じ値を渡すことで、
		// 既定値が何であれ計測と描画が必ず一致する（センタリングのため）。
		const float EffPx = (float)fontPt;
		// 字間（2026-10-12）: 描く側は Slate のフォントの字間で描くので、測る幅にも足す
		const KaedeUI::FKaedeMeasureFn* BaseMeasure = Ctx.Measure;
		KaedeUI::FKaedeMeasureFn SpacedFn;
		const float Sp = Ctx.CurSpacing;
		if (Sp != 0.f && BaseMeasure)
			SpacedFn = [BaseMeasure, Sp](const FString& T, bool b, float px) { return (*BaseMeasure)(T, b, px) + Sp * FMath::Max(0, T.Len() - 1); };
		const KaedeUI::FKaedeMeasureFn* MeasureP = (Sp != 0.f && BaseMeasure) ? &SpacedFn : BaseMeasure;

		// 折り返し指定が無いときだけ効く（wrapW があるなら行に割るのが先）。
		if (bEllipsis && wrapW <= 0 && w > 0)
			Text = EllipsizeToWidth(MeasureP, Text, (float)w, bBold, EffPx);

		KaedeUI::FKaedeDrawCmd Cmd;
		Cmd.Kind = KaedeUI::EKaedeDrawKind::Text;
		Cmd.Text = Text;
		Cmd.bBold = bBold;
		Cmd.Color = ToFColor(c);
		Cmd.FontPt = (float)fontPt;   // 0 のままなら描画側が既定を使う（従来どおり）

		if (wrapW > 0)
		{
			// 本文: 左寄せ・上詰めで複数行に折り返す。各行を Text コマンドとして発行。
			const TArray<FString> Lines =
				MeasureP ? WrapToLines(Text, wrapW, bBold, EffPx, *MeasureP)
				            : TArray<FString>{ Text };
			// lineH 明示指定が最優先。未指定(0)は従来どおり fontPt からの比で算出。
			const int LineH = lineHOverride > 0 ? lineHOverride : LineHFor(fontPt);
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				KaedeUI::FKaedeDrawCmd L = Cmd;
				L.Text = Lines[i];
				L.X = (float)x;
				L.Y = (float)(y + i * LineH);
				if (Ctx.SelRun >= 0) { L.SelGroup = Ctx.SelGroup; L.SelRun = Ctx.SelRun; L.LineH = (float)LineH; }
				AddText(Ctx, L);
			}
			return;
		}

		const bool bCenter = align == "center" || (align.empty() && centerFallback);
		const bool bRight = align == "right";
		float tx = (float)x;
		// 数字を同じ幅で並べる（monoDigits・2026-10-12）: 1 字ずつ置く。数字の幅は「0」の幅
		if ((Ctx.CurMono || Sp != 0.f) && MeasureP)
		{
			const float DigitW = Ctx.CurMono ? (*BaseMeasure)(TEXT("0"), bBold, EffPx) : 0.f;
			TArray<float> Adv;
			float Total = 0.f;
			for (int32 c = 0; c < Text.Len(); ++c)
			{
				const TCHAR Ch = Text[c];
				const float A = (Ctx.CurMono && Ch >= '0' && Ch <= '9') ? DigitW : (*BaseMeasure)(FString(1, &Ch), bBold, EffPx);
				Adv.Add(A + Sp);
				Total += A + Sp;
			}
			Total -= Sp;
			float cx = bCenter ? x + (w - Total) / 2.f : bRight ? x + w - Total : (float)x;
			for (int32 c = 0; c < Text.Len(); ++c)
			{
				KaedeUI::FKaedeDrawCmd One = Cmd;
				One.Text = FString(1, &Text[c]);
				const TCHAR Ch = Text[c];
				// 数字は桁の幅の中で中央に
				const float GW = (*BaseMeasure)(One.Text, bBold, EffPx);
				One.X = cx + ((Ctx.CurMono && Ch >= '0' && Ch <= '9') ? (DigitW - GW) * 0.5f : 0.f);
				One.Y = (float)y; One.CenterH = (float)h; One.bCenterX = false;
				const float SavedSp = Ctx.CurSpacing;
				Ctx.CurSpacing = 0.f;   // 1 字ずつ置いたので、描く側の字間は 0
				AddText(Ctx, One);
				Ctx.CurSpacing = SavedSp;
				cx += Adv[c];
			}
			return;
		}
		const float tw = MeasureP ? (*MeasureP)(Text, bBold, EffPx) : 0.f;
		if (bCenter) tx = x + (w - tw) / 2.f;
		else if (bRight) tx = x + w - tw;
		Cmd.X = tx;
		// 縦位置は描画側で実フォント高を測ってボックス内センタリングする（親からの相対）。
		// 固定オフセット（旧: y + h/2 - 13）はフォント依存でずれるため廃止。
		Cmd.Y = (float)y;
		Cmd.CenterH = (float)h;
		Cmd.bCenterX = bCenter;   // 描画側が字面で横も寄せ直す（SKaedeUiCanvas）
		if (Ctx.SelRun >= 0) { Cmd.SelGroup = Ctx.SelGroup; Cmd.SelRun = Ctx.SelRun; Cmd.LineH = (float)h; }   // 1 行の Label は箱の高さが行
		AddText(Ctx, Cmd);
	}

	// runs（リッチテキスト F1: 区間ごとの色/太字＋ルビ furigana）を純関数 layoutRuns で配置し、
	// 本文片＋（あれば）ルビ片を Text コマンド列にする。ルビは本文の約半分サイズで base 中央上に置く。
	void EmitRuns(FEmitCtx& Ctx, const UiNode& n, const Color& baseCol)
	{
		if (n.runs.empty() || !Ctx.Measure) return;
		// 2026-10-12: 区間の大きさ（size）か行の中の絵（icon）があれば、1 行に並べて下を揃える
		bool bRich = false;
		for (const auto& r : n.runs) if (r.size > 0 || !r.icon.empty()) { bRich = true; break; }
		if (bRich)
		{
			const float BasePt = n.fontPt > 0 ? (float)n.fontPt : 16.f;
			struct FPiece { float W, Pt; const kaedevn::ui::TextRun* R; };
			TArray<FPiece> Pcs;
			float Total = 0.f, MaxPt = 0.f;
			const float Sp = Ctx.CurSpacing;
			for (const auto& r : n.runs)
			{
				const float Pt = r.size > 0 ? (float)r.size : BasePt;
				float W;
				if (!r.icon.empty()) W = Pt * 4.f / 3.f;
				else { const FString Tx = UTF8_TO_TCHAR(r.text.c_str()); W = (*Ctx.Measure)(Tx, r.bold, Pt) + Sp * FMath::Max(0, Tx.Len() - 1); }
				Pcs.Add({W, Pt, &r});
				Total += W;
				MaxPt = FMath::Max(MaxPt, Pt);
			}
			const int BoxW = n.w > 0 ? n.w : 400, BoxH = n.h > 0 ? n.h : 28;
			float cx = n.align == "center" ? n.x + (BoxW - Total) / 2.f : n.align == "right" ? n.x + BoxW - Total : (float)n.x;
			// 下の線: 箱の中で一番大きい字が縦に中央へ来る位置の、字の下
			const float MaxPx = MaxPt * 4.f / 3.f;
			const float Base = n.y + (BoxH + MaxPx) * 0.5f;
			for (const FPiece& Pc : Pcs)
			{
				const float Px = Pc.Pt * 4.f / 3.f;
				if (!Pc.R->icon.empty())
				{
					PushSprite(Ctx, Pc.R->icon, cx, Base - Px * 0.95f, Px, Px, nullptr, false, Pc.R->iconSrc[2] > 0 ? Pc.R->iconSrc : nullptr, 1.f, "contain");
				}
				else
				{
					KaedeUI::FKaedeDrawCmd C;
					C.Kind = KaedeUI::EKaedeDrawKind::Text;
					C.Text = UTF8_TO_TCHAR(Pc.R->text.c_str());
					C.bBold = Pc.R->bold;
					C.Color = ToFColor(Pc.R->hasColor ? Pc.R->color : baseCol);
					C.FontPt = Pc.Pt;
					C.X = cx;
					C.Y = Base - Px * 1.08f;   // 字の箱の上（Slate の行の箱は字の約 1.3 倍・下の線は上から約 1.08 倍）
					AddText(Ctx, C);
				}
				cx += Pc.W;
			}
			return;
		}
		const int wrapW = n.wrapW > 0 ? n.wrapW : (n.w > 0 ? n.w : 400);
		bool hasRuby = false;
		for (const auto& r : n.runs) if (!r.ruby.empty()) { hasRuby = true; break; }
		const int rubyHead = hasRuby ? 16 : 0;
		const float BasePx = (float)n.fontPt;             // 0=ホスト既定（そのまま流す）
		const float RubyPx = n.fontPt > 0 ? (float)n.fontPt * 0.5f : 11.f;  // 本文の約半分
		auto meas = [&Ctx, BasePx](const std::string& s, bool b) -> int
		{ return (int)((*Ctx.Measure)(UTF8_TO_TCHAR(s.c_str()), b, BasePx) + 0.5f); };
		const std::vector<kaedevn::ui::PlacedRun> placed =
			KaedeCore::LayoutRuns(n.runs, n.x, n.y, wrapW, LineHFor(n.fontPt), rubyHead, meas);
		for (const kaedevn::ui::PlacedRun& p : placed)
		{
			KaedeUI::FKaedeDrawCmd C;
			C.Kind = KaedeUI::EKaedeDrawKind::Text;
			C.Text = UTF8_TO_TCHAR(p.text.c_str());
			C.bBold = p.bold;
			C.Color = ToFColor(p.hasColor ? p.color : baseCol);
			C.X = (float)p.x; C.Y = (float)p.y;
			C.FontPt = BasePx;
			if (Ctx.SelRun >= 0) { C.SelGroup = Ctx.SelGroup; C.SelRun = Ctx.SelRun; C.LineH = (float)LineHFor(n.fontPt); }   // ルビ片は選べない
			AddText(Ctx, C);
			if (!p.ruby.empty())
			{
				KaedeUI::FKaedeDrawCmd R;
				R.Kind = KaedeUI::EKaedeDrawKind::Text;
				R.Text = UTF8_TO_TCHAR(p.ruby.c_str());
				R.FontPt = RubyPx;   // ルビは本文の約半分
				R.Color = ToFColor(p.hasColor ? p.color : baseCol);
				// ルビ幅はルビ実サイズで直接測る（本文サイズ×0.5 の近似をやめる）。
				const float rubyW = (*Ctx.Measure)(UTF8_TO_TCHAR(p.ruby.c_str()), false, RubyPx);
				R.X = (float)p.x + (float)p.width * 0.5f - rubyW * 0.5f;
				R.Y = (float)(p.y - rubyHead);
				AddText(Ctx, R);
			}
		}
	}

	void EmitNode(FEmitCtx& Ctx, const UiNode& n);
	void EmitNodeMotion(FEmitCtx& Ctx, const UiNode& n);
	void EmitNodeBody(FEmitCtx& Ctx, const UiNode& n);
	int MeasureStackH(const UiNode& n, const KaedeUI::FKaedeMeasureFn* Measure);

	// 部品の矩形を 1 件記録する（OutRects・2026-10-16）。x/y/w/h は配置が決めた値（宣言は KaedeUiPainter.h の FKaedeNodeRect）
	void AddNodeRect(FEmitCtx& Ctx, const UiNode& n, float x, float y, float w, float h)
	{
		KaedeUI::FKaedeNodeRect R;
		R.Path = UTF8_TO_TCHAR(n.srcPath.c_str());
		R.X = x; R.Y = y; R.W = w; R.H = h;
		R.Depth = Ctx.RectDepth;
		R.bFromRepeat = n.fromRepeat || Ctx.bRectInRepeat;
		R.TemplatePath = n.fromRepeat ? R.Path : Ctx.RectTemplate;
		Ctx.Rects->Add(MoveTemp(R));
	}

	// 部品の矩形（描く処理と同じ決め方）。Modal は中央の窓・h の無い Label と入れ物は測った高さ・Polyline は頂点の外接矩形
	void AddNodeRectOf(FEmitCtx& Ctx, const UiNode& n)
	{
		switch (n.type)
		{
		case UiType::Modal:
		{
			const int pw = n.w > 0 ? n.w : 480;
			const int ph = n.h > 0 ? n.h : 320;
			AddNodeRect(Ctx, n, (float)(n.x > 0 ? n.x : (Ctx.VpW - pw) / 2), (float)(n.y > 0 ? n.y : (Ctx.VpH - ph) / 2), (float)pw, (float)ph);
			return;
		}
		case UiType::Label:
			AddNodeRect(Ctx, n, (float)n.x, (float)n.y, (float)(n.w > 0 ? n.w : 400), (float)MeasureStackH(n, Ctx.Measure));
			return;
		case UiType::VerticalLayout:
		case UiType::HorizontalLayout:
		case UiType::GridLayout:
			AddNodeRect(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)MeasureStackH(n, Ctx.Measure));
			return;
		case UiType::Divider:
			AddNodeRect(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)(n.h > 0 ? n.h : 12));
			return;
		case UiType::Polyline:
			if (n.w <= 0 && n.h <= 0 && !n.points.empty())
			{
				int X0 = n.points[0].x, Y0 = n.points[0].y, X1 = X0, Y1 = Y0;
				for (const UiPoint& P : n.points) { X0 = FMath::Min(X0, P.x); Y0 = FMath::Min(Y0, P.y); X1 = FMath::Max(X1, P.x); Y1 = FMath::Max(Y1, P.y); }
				AddNodeRect(Ctx, n, (float)X0, (float)Y0, (float)(X1 - X0), (float)(Y1 - Y0));
				return;
			}
			break;
		default:
			break;
		}
		AddNodeRect(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h);
	}

	bool IsBoxType(UiType t)
	{
		return t == UiType::Panel || t == UiType::VerticalLayout || t == UiType::HorizontalLayout || t == UiType::GridLayout
			|| t == UiType::ScrollView || t == UiType::Modal || t == UiType::Button;
	}

	// 2026-10-12（rpg-themes/README.md §3.1）: 部品の外側の効果。モーション（EmitNodeMotion）を包む
	void EmitNode(FEmitCtx& Ctx, const UiNode& nIn)
	{
		const UiTheme& T = *Ctx.Theme;
		// ずらす（並べた位置から。中身も一緒に動く＝子の配置は自分の x/y から決まる）
		UiNode Shifted;
		const UiNode* Np = &nIn;
		if (nIn.offset[0] != 0 || nIn.offset[1] != 0)
		{
			Shifted = nIn;
			Shifted.x += nIn.offset[0];
			Shifted.y += nIn.offset[1];
			Np = &Shifted;
		}
		const UiNode& n = *Np;

		// 部品の配置の記録（OutRects）。親が先・中身は 1 段深い。repeat の複製の中では元の template を写す
		const int32 SavedRectDepth = Ctx.RectDepth;
		const bool SavedRectInRepeat = Ctx.bRectInRepeat;
		const FString SavedRectTemplate = Ctx.RectTemplate;
		if (Ctx.Rects)
		{
			AddNodeRectOf(Ctx, n);
			++Ctx.RectDepth;
			if (n.fromRepeat) { Ctx.bRectInRepeat = true; Ctx.RectTemplate = UTF8_TO_TCHAR(n.srcPath.c_str()); }
		}

		// 文字の効果（部品 > 親から受け継いだもの > テーマ）。中身に受け継ぐ
		const kaedevn::ui::UiTextOutline SavedOutline = Ctx.CurOutline;
		const kaedevn::ui::UiTextGlow SavedGlow = Ctx.CurGlow;
		const float SavedSpacing = Ctx.CurSpacing;
		const bool SavedMono = Ctx.CurMono, SavedHi = Ctx.bHiText;
		const Color SavedHiText = Ctx.HiText;
		if (n.textOutline.on) Ctx.CurOutline = n.textOutline;
		if (n.textGlow.on) Ctx.CurGlow = n.textGlow;
		if (n.letterSpacing >= 0.f) Ctx.CurSpacing = n.letterSpacing;
		if (n.monoDigits) Ctx.CurMono = true;
		if (n.highlight && IsBoxType(n.type))
		{
			if (n.hasHighlightTextColor) { Ctx.bHiText = true; Ctx.HiText = n.highlightTextColor; }
			else if (T.select.on && T.select.hasText) { Ctx.bHiText = true; Ctx.HiText = T.select.text; }
		}

		const int32 Start = Ctx.Out->Num();
		if (n.backdropBlur > 0.f && n.w > 0 && n.h > 0)
		{
			KaedeUI::FKaedeDrawCmd Bl;
			Bl.Kind = KaedeUI::EKaedeDrawKind::Blur;
			Bl.X = (float)n.x; Bl.Y = (float)n.y; Bl.W = (float)n.w; Bl.H = (float)n.h;
			Bl.Blur = n.backdropBlur;
			Ctx.Out->Add(Bl);
		}
		// 光の輪（箱の部品は PaintBox が描く）
		if (n.glow.on && !IsBoxType(n.type) && n.w > 0 && n.h > 0) PushGlow(Ctx, n.x, n.y, n.w, n.h, n.glow);
		if (n.clip && n.w > 0 && n.h > 0)
		{
			KaedeUI::FKaedeDrawCmd Cp;
			Cp.Kind = KaedeUI::EKaedeDrawKind::ClipPush;
			Cp.X = (float)n.x; Cp.Y = (float)n.y; Cp.W = (float)n.w; Cp.H = (float)n.h;
			Cp.ContentH = (float)n.h;   // 中身は箱と同じ高さ＝スクロールしない
			Ctx.Out->Add(Cp);
		}
		const bool bDecorBox = n.variant == "dark" || n.variant == "light" || n.type == UiType::Modal;
		if (bDecorBox) Ctx.KnockOpen.Add(Ctx.KnockSeq++);   // 自分の窓は、中身にとって祖先

		EmitNodeMotion(Ctx, n);

		if (bDecorBox) Ctx.KnockOpen.Pop();
		if (n.clip && n.w > 0 && n.h > 0)
		{
			KaedeUI::FKaedeDrawCmd Pp;
			Pp.Kind = KaedeUI::EKaedeDrawKind::ClipPop;
			Ctx.Out->Add(Pp);
		}
		// 中身ごと薄くする・回す・斜めにする
		if (n.opacity < 1.f || n.rotate != 0.f || n.skewX != 0.f)
		{
			const float Cx = n.x + n.w * 0.5f, Cy = n.y + n.h * 0.5f;
			for (int32 i = Start; i < Ctx.Out->Num(); ++i)
			{
				KaedeUI::FKaedeDrawCmd& C = (*Ctx.Out)[i];
				if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPush || C.Kind == KaedeUI::EKaedeDrawKind::ClipPop) continue;
				if (n.opacity < 1.f)
				{
					C.Color.A = (uint8)FMath::RoundToInt(C.Color.A * n.opacity);
					C.OutlineColor.A = (uint8)FMath::RoundToInt(C.OutlineColor.A * n.opacity);
					C.Alpha *= n.opacity;
					for (FColor& G : C.GradColors) G.A = (uint8)FMath::RoundToInt(G.A * n.opacity);
					for (FColor& G : C.PointColors) G.A = (uint8)FMath::RoundToInt(G.A * n.opacity);
				}
				if ((n.rotate != 0.f || n.skewX != 0.f) && C.Angle == 0.f && C.SkewX == 0.f)
				{
					C.Angle = n.rotate; C.SkewX = n.skewX; C.XfX = Cx; C.XfY = Cy;
				}
			}
		}
		Ctx.CurOutline = SavedOutline; Ctx.CurGlow = SavedGlow; Ctx.CurSpacing = SavedSpacing; Ctx.CurMono = SavedMono;
		Ctx.bHiText = SavedHi; Ctx.HiText = SavedHiText;
		Ctx.RectDepth = SavedRectDepth; Ctx.bRectInRepeat = SavedRectInRepeat; Ctx.RectTemplate = SavedRectTemplate;
	}

	// EmitNode — UIモーションのラッパー（設計 07/21/04）。Motion 未使用なら実質素通り。
	// 1. anim 付きノードに安定キーを振る: key = scope + "/" + (id があれば id / 無ければ action)
	//    ＝「どのノードか」に紐づける。番号・座標・構造パスには決して紐づけない。
	// 2. UiMotionState へ登録（enter 発火 / countUp 値検知 / pulse 条件評価）
	// 3. 配下の描画コマンドへ MotionKey/Pivot を付与（合成は描画側が毎フレーム行う）
	// ネストした anim は外側優先（内側は無視）。ヒット矩形には付与しない（最終位置基準）。
	// id も action も無い anim ノードは検証エラー（§A2）＝登録しない。フォールバックは作らない。
	void EmitNodeMotion(FEmitCtx& Ctx, const UiNode& n)
	{
		if (!Ctx.Motion)
		{
			EmitNodeBody(Ctx, n);
			return;
		}

		// このノードの「名前」（id 優先・無ければ action。repeat 補間後＝データの同一性）。
		const std::string& Name = !n.id.empty() ? n.id : n.action;
		const int32 CmdStart = Ctx.Out->Num();
		const int32 HitStart = Ctx.Hits ? Ctx.Hits->Num() : 0;
		std::string Key;
		if (n.anim && n.anim->any() && Ctx.ActiveMotionKey.empty())
		{
			if (Name.empty())
				Ctx.Motion->reportError("key-missing scope=" + Ctx.MotionScreenId +
					" group=" + Ctx.MotionGroupKey + " text=" + n.text);
			else
			{
				Key = Ctx.MotionScreenId + "/" + Name;
				Ctx.ActiveMotionKey = Key;
			}
		}

		// 子の stagger グループ＝自分（名前があれば）。repeat 展開後の行は、展開元のリスト
		// ノードのキーを共有する＝同じ階段で並ぶ（設計 §B3）。
		const std::string SavedGroupKey = Ctx.MotionGroupKey;
		if (!Name.empty()) Ctx.MotionGroupKey = Ctx.MotionScreenId + "/" + Name;

		EmitNodeBody(Ctx, n);

		Ctx.MotionGroupKey = SavedGroupKey;

		// 押下フィードバック（B1・設計 07/21/02）: この node 自身が登録したヒットへ
		// 配下コマンドを紐づける。子が先に自分のヒットへ紐づけ済みなら上書きしない
		//（深い方＝実際に押されるボタンが勝つ）。ヒット矩形は動かさない（既決）。
		if (Ctx.Hits && !n.action.empty())
		{
			const FString ActionF = UTF8_TO_TCHAR(n.action.c_str());
			int32 HitIdx = INDEX_NONE;
			for (int32 i = HitStart; i < Ctx.Hits->Num(); ++i)
				if ((*Ctx.Hits)[i].Action == ActionF) { HitIdx = i; break; }
			if (HitIdx != INDEX_NONE)
			{
				for (int32 i = CmdStart; i < Ctx.Out->Num(); ++i)
				{
					KaedeUI::FKaedeDrawCmd& C = (*Ctx.Out)[i];
					if (C.HitIndex >= 0) continue;
					if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPush ||
						C.Kind == KaedeUI::EKaedeDrawKind::ClipPop) continue;
					C.HitIndex = HitIdx;
				}
			}
		}

		if (!Key.empty())
		{
			Ctx.ActiveMotionKey.clear();

			// countUp: バインド済みテキストが数値（"1,234" の桁区切り可）のときだけ補間対象。
			bool bHasNum = false, bGrouped = false;
			double Num = 0.0;
			if (n.anim->hasCountUp && !n.text.empty())
				bHasNum = ReadTextNumber(n.text, true, Num, &bGrouped);
			const bool bPulseOn = n.anim->hasPulse
				&& (n.anim->pulseWhileIf.empty()
					|| (Ctx.MotionCond && (*Ctx.MotionCond)(n.anim->pulseWhileIf)));

			Ctx.Motion->noteNode(Key, SavedGroupKey, *n.anim, bHasNum, Num, bGrouped, bPulseOn);
			if (n.anim->hasTween && n.type == UiType::Slider) Ctx.Motion->noteValue(Key, (double)n.value);

			const FString KeyF = UTF8_TO_TCHAR(Key.c_str());
			const float Px = (float)n.x + (float)n.w * 0.5f;
			const float Py = (float)n.y + (float)n.h * 0.5f;
			for (int32 i = CmdStart; i < Ctx.Out->Num(); ++i)
			{
				KaedeUI::FKaedeDrawCmd& C = (*Ctx.Out)[i];
				// ClipPush/Pop は動かさない（スクロールのクリップ域が二重に動くため）。
				if (!C.MotionKey.IsEmpty()) continue;
				if (C.Kind == KaedeUI::EKaedeDrawKind::ClipPush ||
					C.Kind == KaedeUI::EKaedeDrawKind::ClipPop) continue;
				C.MotionKey = KeyF;
				C.PivotX = Px;
				C.PivotY = Py;
			}
		}
	}

	// ScrollView 縦積み用: 子の実効高さ。h 未指定のコンテナ（repeat 展開後の VerticalLayout 等）
	// は中身から再帰で導出する。従来は h 未指定を一律 28 扱いで積んでいたため、依頼ボードの
	// セクション（h なしの repeat コンテナ）が28px刻みで重なって描かれた（07/20 実害）。
	// Measure は折り返す Label の高さを測るための計測（F1・2026-10-03）。配置は Ctx.Measure を渡す。nullptr なら折り返しを数えない。
	int MeasureStackH(const UiNode& n, const KaedeUI::FKaedeMeasureFn* Measure)
	{
		if (n.h > 0) return n.h;
		switch (n.type)
		{
		case UiType::Label:
		{
			// 折り返す Label（wrapW>0・h 無し）は、描くときと同じ折り返しで行数を数えて高さにする（F1）。
			// 字の太さは描画と同じ解決（FontIdFor(n, "bold")）。h を JSON や host が推定しなくてよい。
			if (n.wrapW > 0 && Measure && !n.text.empty())
			{
				const FString Text = UTF8_TO_TCHAR(n.text.c_str());
				const int32 Lines = FMath::Max(1, WrapToLines(Text, n.wrapW, IsBold(FontIdFor(n, "bold")), (float)n.fontPt, *Measure).Num());
				return Lines * (n.lineH > 0 ? n.lineH : LineHFor(n.fontPt));
			}
			return 28;
		}
		case UiType::VerticalLayout:
		{
			int h = 2 * n.pad;
			for (const auto& c : n.children) h += 2 * c.margin + MeasureStackH(c, Measure) + n.gap;
			if (!n.children.empty()) h -= n.gap;
			return FMath::Max(h, 0);
		}
		case UiType::HorizontalLayout:
		{
			int maxH = 0;
			for (const auto& c : n.children)
				maxH = FMath::Max(maxH, 2 * c.margin + MeasureStackH(c, Measure));
			return 2 * n.pad + maxH;
		}
		case UiType::GridLayout:
		{
			// cols 列で折り返した行数 × 行高（子の最大高さ）。h 未指定のグリッドを
			// ScrollView / VerticalLayout の中身として正しく積めるようにする。
			// これが無いと既定の 28 が返り、グリッドの h を画面JSONへ直書きするしかなく、
			// 要素数が増えた時に下が切れる/余白が残る（設計 07/27/03 §6.2）。
			// h>0 が指定されていれば冒頭の early return が優先＝後方互換。
			if (n.children.empty()) return 2 * n.pad;
			const int cols = FMath::Max(1, n.cols);
			int rowH = 0;
			for (const auto& c : n.children)
				rowH = FMath::Max(rowH, 2 * c.margin + MeasureStackH(c, Measure));
			const int rows = ((int)n.children.size() + cols - 1) / cols;
			return 2 * n.pad + rows * rowH + (rows - 1) * n.gap;
		}
		case UiType::Divider:
			return 12;
		default:
			return 28;   // 従来既定
		}
	}

	// 部品 1 つぶんの描画（型ごとの Emit*Node へ振り分ける）。
	// 再帰は EmitNode（下のモーションラッパー）経由。本体は直接呼ばない。
	void EmitNodeBodyInner(FEmitCtx& Ctx, const UiNode& n);

	// 文字の選択の器（2026-10-07・設計 10/07/01 §3・§4.1）。selectable の Panel / VerticalLayout / ScrollView に入るとき、
	// 器の矩形（SelectArea）を出し、中の Label の行に器の鍵を写す。入れ子なら内側の器が勝つ（出るとき外の器へ戻す）。
	// SelectArea は器の描画より先に出す＝ScrollView なら ClipPush の前（器は親の座標にあり、親のスクロールにだけ従う）
	void EmitNodeBody(FEmitCtx& Ctx, const UiNode& n)
	{
		const bool bSel = n.selectable
			&& (n.type == UiType::Panel || n.type == UiType::VerticalLayout || n.type == UiType::ScrollView);
		if (!bSel) { EmitNodeBodyInner(Ctx, n); return; }
		const FString SavedGroup = Ctx.SelGroup;
		const int32 SavedSeq = Ctx.SelRunSeq, SavedRun = Ctx.SelRun;
		Ctx.SelGroup = !n.id.empty() ? (TEXT("id:") + FString(UTF8_TO_TCHAR(n.id.c_str()))) : FString::Printf(TEXT("ord:%d"), Ctx.SelOrd++);
		Ctx.SelRunSeq = 0;
		Ctx.SelRun = -1;
		KaedeUI::FKaedeDrawCmd Area;
		Area.Kind = KaedeUI::EKaedeDrawKind::SelectArea;
		Area.X = (float)n.x; Area.Y = (float)n.y; Area.W = (float)n.w; Area.H = (float)n.h;
		Area.SelGroup = Ctx.SelGroup;
		Area.FocusGroup = Ctx.FocusGroup;
		Ctx.Out->Add(Area);
		EmitNodeBodyInner(Ctx, n);
		Ctx.SelGroup = SavedGroup;
		Ctx.SelRunSeq = SavedSeq;
		Ctx.SelRun = SavedRun;
	}

	// ---- 部品ごとの描画（EmitNodeBodyInner の case を 1 つずつ関数にした・2026-10-18 Issue #73）----

	// 入れ物（Panel・各 Layout・ScrollView）の地を描く。色は color → variant light（panelLight）/ dark（track）→ panel
	void PaintContainerBox(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		const Color Base = n.hasColor ? n.color
		                 : (n.variant == "light" ? T.panelLight
		                    : n.variant == "dark" ? T.track : T.panel);
		PaintBox(Ctx, n.x, n.y, n.w, n.h, n.highlight ? HighlightFill(Base, T) : Base, n.highlight ? T.accent : T.panelEdge, BoxRad(Ctx, kRadPanel),
		         n.variant == "light" || n.variant == "dark", n.highlight, &n);
	}

	// 入れ物（VerticalLayout / HorizontalLayout / GridLayout / ScrollView）の地。色・highlight・light/dark のときだけ描く
	void EmitLayoutBox(FEmitCtx& Ctx, const UiNode& n)
	{
		if (EmitContainerStandard(Ctx, n) || !(n.hasColor || n.highlight || n.variant == "light" || n.variant == "dark")) return;
		PaintContainerBox(Ctx, n);
	}

	void EmitChildren(FEmitCtx& Ctx, const UiNode& n)
	{
		for (const auto& c : n.children) EmitNode(Ctx, c);
	}

	// ---- 図形（F-2）----
	// レイアウトには参加しない。x/y は常に絶対座標（親が Panel でも位置を書き換えない）。
	// 攻略地図のような「位置と線が意味を持つ図」は、生成器が座標を全部決めて渡すため。
	void EmitPolylineNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		if (n.points.size() >= 2)
		{
			KaedeUI::FKaedeDrawCmd Cmd;
			Cmd.Kind = KaedeUI::EKaedeDrawKind::Lines;
			Cmd.Color = ToFColor(n.hasColor ? n.color : T.textPrimary);
			Cmd.Thickness = FMath::Max(n.thickness, 0.1f);
			Cmd.Points.Reserve((int32)n.points.size() + (n.closed ? 1 : 0));
			for (const UiPoint& P : n.points) Cmd.Points.Add(FVector2f((float)P.x, (float)P.y));
			// 閉じた折れ線＝枠線を1ノードで描く。
			// 始点は**必ずコピーしてから**足す。Add(Cmd.Points[0]) は「追加先の配列の要素」を
			// 渡す形になり、UE のコンテナ検査がアサートで落とす（再確保の有無に関係なく）。
			if (n.closed)
			{
				const FVector2f First = Cmd.Points[0];
				Cmd.Points.Add(First);
			}
			Ctx.Out->Add(Cmd);
		}
		EmitChildren(Ctx, n);
	}

	void EmitPolygonNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// 塗りの多角形（points は自分の左上から）。並べる入れ物の中に置ける
		if (n.points.size() >= 3)
		{
			TArray<FVector2f> Pts;
			for (const UiPoint& P : n.points) Pts.Add(FVector2f((float)(n.x + P.x), (float)(n.y + P.y)));
			PushPolygon(Ctx, Pts, n.hasColor ? n.color : T.panel, n.gradient.on ? &n.gradient : nullptr);
			if (n.borderWidth > 0 || n.hasBorderColor)
				PushPolyOutline(Ctx, Pts, n.hasBorderColor ? n.borderColor : T.panelEdge, (float)(n.borderWidth > 0 ? n.borderWidth : 1));
		}
		if (n.enabled && !n.action.empty() && n.w > 0 && n.h > 0)
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		EmitChildren(Ctx, n);
	}

	void EmitDividerNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// 区切り線（中央に線・ornament で中央と両端に菱形）
		const int H = n.h > 0 ? n.h : 12;
		const float Th = FMath::Max(1.f, n.thickness);
		const Color LC = n.hasColor ? n.color : T.panelEdge;
		const float Cy = n.y + H * 0.5f;
		if (n.gradient.on) PushGradient(Ctx, n.x, (int)(Cy - Th * 0.5f), n.w, (int)FMath::Max(1.f, Th), n.gradient, 0.f);
		else PushRect(Ctx, n.x, (int)(Cy - Th * 0.5f), n.w, (int)FMath::Max(1.f, Th), LC, 0.f);
		auto Diamond = [&](float cx, float r)
		{
			PushPolygon(Ctx, {{cx, Cy - r}, {cx + r, Cy}, {cx, Cy + r}, {cx - r, Cy}}, LC);
		};
		if (n.ornament == "diamond") { Diamond(n.x + n.w * 0.5f, 5.f); Diamond(n.x + n.w * 0.5f - 9.f, 2.5f); Diamond(n.x + n.w * 0.5f + 9.f, 2.5f); }
		else if (n.ornament == "dots") { Diamond((float)n.x + 3.f, 3.f); Diamond(n.x + n.w * 0.5f, 3.f); Diamond(n.x + n.w - 3.f, 3.f); }
	}

	void EmitEllipseNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		if (n.w > 0 && n.h > 0)
		{
			KaedeUI::FKaedeDrawCmd Cmd;
			Cmd.Kind = KaedeUI::EKaedeDrawKind::Ellipse;
			Cmd.X = (float)n.x; Cmd.Y = (float)n.y; Cmd.W = (float)n.w; Cmd.H = (float)n.h;
			Cmd.Color = ToFColor(n.hasColor ? n.color : T.textPrimary);
			Cmd.bFill = n.fill;
			Cmd.Thickness = FMath::Max(n.thickness, 0.1f);
			Ctx.Out->Add(Cmd);
		}
		EmitChildren(Ctx, n);
	}

	void EmitPanelNode(FEmitCtx& Ctx, const UiNode& n)
	{
		// variant "spacer"=レイアウト詰め物（何も描かない）/ "light"=panelLight 塗り /
		// "dark"=track 塗り（帯プレート見出し用。highlight と併用で金枠帯）/
		// pc98-*・atelier-*=Component Standard（EmitContainerStandard）。
		if (n.variant != "spacer" && !EmitContainerStandard(Ctx, n))
			PaintContainerBox(Ctx, n);
		// action 付き Panel はクリック領域を登録する（多層フレームのカード/ナビをボタン化する
		// UE 先行結線。action は既契約フィールドで、意味論は Button と同じ）。
		if (n.enabled && !n.action.empty() && n.variant != "spacer")
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		EmitChildren(Ctx, n);
	}

	void EmitImageNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Component Standard 画像拡張（Phase 3・CodexReports/2026/07/12-11）。
		//   variant が icon-tile/slot-card 等のロールなら先に器（枠）を描き、内側に画像を収める。
		//   sprite あり → Image コマンド（描画側が Fit で配置・失敗時プレースホルダ）。
		//   sprite なし → 器のみ＋fallback（iconChar 相当は text 先頭1文字、無ければ "?"）。
		const std::string& v = n.variant;
		const bool bSlot = v == "atelier-slot-card" || v == "pc98-slot-card" || v == "slot-card";
		const bool bTile = ContainerRoleFor(v, Ctx) == EContainerRole::Tile || bSlot;
		int ix = n.x, iy = n.y, iw = n.w, ih = n.h;   // 画像の内枠（器の内側）
		if (bTile)
		{
			// 器は Tile ロールで描く（slot-card は角丸大きめだが Tile 流用で足りる）。
			UiNode Frame = n; Frame.variant = bSlot ? "atelier-icon-tile" : v;
			if (ContainerRoleFor(Frame.variant, Ctx) != EContainerRole::Tile)
				Frame.variant = "atelier-icon-tile";
			EmitContainerStandard(Ctx, Frame);
			const int pad = 6;
			ix = n.x + pad; iy = n.y + pad; iw = n.w - 2 * pad; ih = n.h - 2 * pad;
		}
		else if (n.sprite.empty() && !n.hasColor)
		{
			// ロール無し・色無し・画像無しの素の Image は従来どおり panel 塗り。
			PushRect(Ctx, n.x, n.y, n.w, n.h, T.panel);
		}
		else if (n.gradient.on && n.sprite.empty())
		{
			PushGradient(Ctx, n.x, n.y, n.w, n.h, n.gradient, 0.f);
		}
		else if (n.hasColor)
		{
			PushRect(Ctx, n.x, n.y, n.w, n.h, n.color);
		}

		if (!n.sprite.empty())
		{
			KaedeUI::FKaedeDrawCmd Cmd;
			Cmd.Kind = KaedeUI::EKaedeDrawKind::Image;
			Cmd.X = (float)ix; Cmd.Y = (float)iy; Cmd.W = (float)iw; Cmd.H = (float)ih;
			Cmd.Sprite = UTF8_TO_TCHAR(n.sprite.c_str());
			Cmd.Fit = n.fit.empty() ? FString(TEXT("contain")) : UTF8_TO_TCHAR(n.fit.c_str());
			// 2026-10-12: 9 分割・敷き詰め・切り出し
			if (n.hasSlice) { for (int k = 0; k < 4; ++k) Cmd.Slice[k] = (float)n.slice[k]; Cmd.Fit = TEXT("stretch"); }
			Cmd.bTile = n.tile;
			if (n.hasSrc) for (int k = 0; k < 4; ++k) Cmd.Src[k] = (float)n.src[k];
			Ctx.Out->Add(Cmd);
		}
		// 枠（borderColor・borderWidth を書いたときだけ。角は直角・画像の上に描く）
		if (n.borderWidth > 0 || (n.hasBorderColor && n.borderWidth != 0))
		{
			const int B = n.borderWidth > 0 ? n.borderWidth : 1;
			const Color& E = n.hasBorderColor ? n.borderColor : T.panelEdge;
			for (int i = 0; i < B && n.w - 2 * i > 0 && n.h - 2 * i > 0; ++i)
				PushOutline(Ctx, n.x + i, n.y + i, n.w - 2 * i, n.h - 2 * i, E, 0.f);
		}
		// action 付き Image はクリック領域を登録する（Panel と同じ既契約フィールドの結線。
		// 図鑑の回転表示「タップで連番送り」等に使う）。
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		else if (bTile)
		{
			// 画像なしタイルの fallback: text 先頭1コードポイント（無ければ "?"）を中央に。
			std::string fb = n.text.empty() ? std::string("?")
			                                 : n.text.substr(0, kaedevn::ui::utf8CharLen((unsigned char)n.text[0]));
			EmitLabel(Ctx, fb, n.x, n.y, n.w, n.h,
			          n.hasTextColor ? n.textColor : T.accent, "center", true, FontIdFor(n, "bold"), 0,
			          n.fontPt);
		}
		EmitChildren(Ctx, n);
	}

	void EmitLabelNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		const Color lc = n.hasTextColor ? n.textColor
		                                : (n.enabled ? T.textPrimary : T.textDim);
		// selectable の器の中なら、この Label の行は選べる（Label ごとに番号を 1 つ・設計 10/07/01 §4.1）
		const int32 SavedSelRun = Ctx.SelRun;
		if (!Ctx.SelGroup.IsEmpty()) Ctx.SelRun = Ctx.SelRunSeq++;
		// runs があれば区間装飾＋ルビで描く（F1）。無ければ従来のフラット text。
		if (!n.runs.empty()) EmitRuns(Ctx, n, lc);
		else EmitLabel(Ctx, n.text, n.x, n.y, n.w > 0 ? n.w : 400, n.h > 0 ? n.h : 28,
		               lc, n.align, n.center, FontIdFor(n, "bold"), n.wrapW, n.fontPt, n.lineH, n.ellipsis);
		Ctx.SelRun = SavedSelRun;
	}

	void EmitButtonNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Component Standard の Button variant はヘルパで完結（非対応なら従来パス）。
		if (EmitButtonStandard(Ctx, n)) return;

		// 2026-10-12（rpg-themes/README.md §3）: "plain"＝文字だけ（行の一覧の項目）・"box"＝窓と同じ描き方
		//（shape・gradient・borderColor・edge3d など）。選んでいる見た目は焦点（テーマの select）が付ける
		if (n.variant == "plain" || n.variant == "box")
		{
			const std::string lbl = n.icon.empty() ? n.text : (n.icon + " " + n.text);
			if (n.variant == "box")
				PaintBox(Ctx, n.x, n.y, n.w, n.h, n.hasColor ? n.color : T.panel, n.highlight ? T.accent : T.panelEdge,
				         BoxRad(Ctx, kRadButton), false, n.highlight, &n);
			const std::string Al = n.align.empty() ? (n.variant == "plain" ? "left" : "center") : n.align;
			const int Pad = Al == "center" ? 0 : 12;
			EmitLabel(Ctx, lbl, n.x + Pad, n.y, n.w - 2 * Pad, n.h,
			          n.hasTextColor ? n.textColor : (n.enabled ? T.textPrimary : T.textDim),
			          Al, true, FontIdFor(n, "regular"), 0, n.fontPt, 0, n.ellipsis);
			if (n.enabled && !n.action.empty())
				AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
			return;
		}

		const bool bPrimary = n.variant != "ghost";
		const std::string lbl = n.icon.empty() ? n.text : (n.icon + " " + n.text);
		if (bPrimary)
		{
			const Color bg = n.hasColor ? n.color : (n.enabled ? T.accent : T.accentDim);
			PushRect(Ctx, n.x, n.y, n.w, n.h, bg, BoxRad(Ctx, kRadButton));
			EmitLabel(Ctx, lbl, n.x, n.y, n.w, n.h,
			          n.hasTextColor ? n.textColor : (n.enabled ? T.textOnAccent : T.textDim),
			          "center", true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
		}
		else
		{
			PushRect(Ctx, n.x, n.y, n.w, n.h, n.hasColor ? n.color : T.panel, BoxRad(Ctx, kRadButton));
			// highlight=true の ghost は accent 枠（目次の現在位置等。UE 先行拡張）。
			PushOutline(Ctx, n.x, n.y, n.w, n.h, n.highlight ? T.accent : T.panelEdge, BoxRad(Ctx, kRadButton));
			// 字の寄せ。既定は中央。align=left/right は左右に 10px の内側余白を取って寄せる
			//（2026-10-02: 事件の図の札。Label を重ねなくても文章を左寄せで出せるように）。
			const bool bSide = n.align == "left" || n.align == "right";
			EmitLabel(Ctx, lbl, bSide ? n.x + 10 : n.x, n.y, bSide ? n.w - 20 : n.w, n.h,
			          n.hasTextColor ? n.textColor : (n.enabled ? T.textPrimary : T.textDim),
			          n.align.empty() ? "center" : n.align, true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
		}
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
	}

	void EmitSliderNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Slider: track 全幅 + value ぶんの accent 塗り。
		const int h = n.h > 0 ? n.h : 14;
		// text に数があれば値（2026-10-12: 一覧の中のゲージ "{item.hpRate}"。bindVar は一覧の中で差し込まれない）
		float vIn = n.value;
		double D = 0.0;
		if (!n.text.empty() && ReadTextNumber(n.text, false, D)) vIn = (float)D;
		const float v = FMath::Clamp(vIn, 0.f, 1.f);
		const int fw = (int)(n.w * v);
		const Color FillC = n.hasColor ? n.color : T.accent;
		if (IsShaped(n.shape))
		{
			// 2026-10-12: 形のあるゲージ（両端の尖った枠など）。棒も同じ形で切る
			const TArray<FVector2f> Track = ShapePoints(n.shape, n.x, n.y, n.w, h, n.shapeInset);
			PushPolygon(Ctx, Track, T.track);
			if (fw > 0)
			{
				const int In = n.shapeInset >= 0 ? n.shapeInset : (int)(h * 0.35f);
				const int pad = FMath::Max(2, n.borderWidth > 0 ? n.borderWidth + 1 : 2);
				if (fw > 2 * In + 2 * pad)
					PushPolygon(Ctx, ShapePoints(n.shape, n.x + pad, n.y + pad, fw - 2 * pad, h - 2 * pad, FMath::Max(0, In - pad)), FillC, n.gradient.on ? &n.gradient : nullptr);
			}
			if (n.borderWidth != 0) PushPolyOutline(Ctx, Track, n.hasBorderColor ? n.borderColor : T.panelEdge, (float)(n.borderWidth > 0 ? n.borderWidth : 1));
		}
		else
		{
			const float SR = SkinRad(Ctx, h * 0.5f);
			PushRect(Ctx, n.x, n.y, n.w, h, T.track, SR);
			const int32 FillStart = Ctx.Out->Num();
			if (fw > 0)
			{
				if (n.gradient.on) PushGradient(Ctx, n.x, n.y, fw, h, n.gradient, SR);
				else PushRect(Ctx, n.x, n.y, fw, h, FillC, SR);
			}
			for (int32 k = FillStart; k < Ctx.Out->Num(); ++k) (*Ctx.Out)[k].bFillBar = true;
			if (n.edge3d.on) PushEdge3d(Ctx, n.x, n.y, n.w, h, n.edge3d, 0);
			if (n.borderWidth > 0 || n.hasBorderColor)
				PushOutline(Ctx, n.x, n.y, n.w, h, n.hasBorderColor ? n.borderColor : T.panelEdge, SR, (float)(n.borderWidth > 0 ? n.borderWidth : 1));
		}
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)h, true);
	}

	void EmitToggleNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Toggle: チェックボックス（枠 + on 時は内側 accent）。
		const int box = n.h > 0 ? n.h : 28;
		PushRect(Ctx, n.x, n.y, box, box, T.panel, SkinRad(Ctx, kRadToggle));
		PushOutline(Ctx, n.x, n.y, box, box, n.on ? T.accent : T.panelEdge, SkinRad(Ctx, kRadToggle));
		if (n.on) PushRect(Ctx, n.x + 5, n.y + 5, box - 10, box - 10, T.accent, kRadToggle - 2.f);
		if (!n.text.empty())
			EmitLabel(Ctx, n.text, n.x + box + 10, n.y, 300, box,
			          T.textPrimary, "left", false, FontIdFor(n, "main"), 0, n.fontPt);
		if (Ctx.Hits && n.enabled && !n.action.empty())
		{
			// 右側ラベルぶんも当たりに含める（box + 10 + textW）。
			float tw = 0.f;
			if (!n.text.empty() && Ctx.Measure)
				tw = (*Ctx.Measure)(UTF8_TO_TCHAR(n.text.c_str()), false, (float)n.fontPt);
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)(box + 10) + tw, (float)box, true);
		}
	}

	void EmitToggleGroupNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// ToggleGroup: 子を横等分・selected に上部 accent バー。
		const int cnt = (int)n.children.size();
		if (cnt == 0) return;
		const int cellW = n.w / cnt;
		for (int i = 0; i < cnt; ++i)
		{
			const UiNode& c = n.children[i];
			const int cx = n.x + i * cellW;
			const bool bOn = (i == n.selected);
			if (bOn) PushRect(Ctx, cx, n.y, cellW, 3, T.accent);
			if (Ctx.Rects) AddNodeRect(Ctx, c, (float)cx, (float)n.y, (float)cellW, (float)n.h);   // 子は EmitNode を通らない
			EmitLabel(Ctx, c.text, cx, n.y, cellW, n.h,
			          bOn ? T.textPrimary : T.textDim, "center", true, FontIdFor(c, "main"), 0, c.fontPt);
			if (c.enabled && !c.action.empty())
				AddHit(Ctx, c, (float)cx, (float)n.y, (float)cellW, (float)n.h, true);
		}
	}

	void EmitModalNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Modal: 暗幕 + 中央パネル + 子の縦積み。
		// 背面吸収のため action 無しでも全画面 backdrop hit を積む（host は後勝ちで判定）。
		// Focus は Modal 内に閉じ込めるため、backdrop も子も Modal の群に置く（2026-10-03: backdrop も中の群に
		// したので、操作対象の無い Modal でも最前面の群が上がり、背景へ Enter が漏れない。仕様 §7）。
		// 群の番号は**描いた順の通し番号**（2026-10-04・Codex 10/04/01 F4）: 入れ子の深さだと同じ親の Modal 2 つが同じ群になり、
		// 画面で手前（後に描いた）の Modal を覆われた側の部品が焦点を取れた。通し番号なら後に描いた Modal が常に最前面。
		if (n.dim) PushRect(Ctx, 0, 0, Ctx.VpW, Ctx.VpH, T.dim);
		const int32 SavedGroup = Ctx.FocusGroup;
		Ctx.FocusGroup = ++Ctx.ModalSeq;
		AddHit(Ctx, n, 0.f, 0.f, (float)Ctx.VpW, (float)Ctx.VpH, /*bFocusable*/ false);
		const int pw = n.w > 0 ? n.w : 480;
		const int ph = n.h > 0 ? n.h : 320;
		const int px = n.x > 0 ? n.x : (Ctx.VpW - pw) / 2;
		const int py = n.y > 0 ? n.y : (Ctx.VpH - ph) / 2;
		PaintBox(Ctx, px, py, pw, ph, n.hasColor ? n.color : T.panel, T.panelEdge, BoxRad(Ctx, kRadModal),
		         true, false, &n);
		if (!n.text.empty())
			EmitLabel(Ctx, n.text, px, py + 4, pw, 36,
			          T.textPrimary, "center", true, FontIdFor(n, "bold"), 0, n.fontPt, 0, n.ellipsis);
		int cy = py + (n.text.empty() ? n.pad : 48);
		for (const auto& cc : n.children)
		{
			UiNode c = cc;  // 子をコピーして配置を上書き（native と同じ）
			cy += c.margin;
			c.x = px + n.pad + c.margin;
			if (c.w == 0) c.w = pw - 2 * n.pad - 2 * c.margin;
			c.y = cy;
			EmitNode(Ctx, c);
			cy += MeasureStackH(c, Ctx.Measure) + c.margin + n.gap;   // h未指定コンテナは中身から導出（07/20）
		}
		Ctx.FocusGroup = SavedGroup;
	}

	void EmitVerticalLayoutNode(FEmitCtx& Ctx, const UiNode& n)
	{
		EmitLayoutBox(Ctx, n);
		// action 付きレイアウトはカード全体をボタン化（Panel と同じ UE 先行結線。
		// 子のボタン hit の方が後に積まれるため、子が優先で当たる）。
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		// h 未指定の非flex子（repeat 展開後のコンテナ等）は MeasureStackH で中身から導出
		//（ScrollView と同じ 07/20 修正。従来の一律28では repeat コンテナが後続と重なる）。
		float sumFlex = 0; int fixed = 0;
		for (const auto& c : n.children)
		{
			sumFlex += c.flex;
			fixed += 2 * c.margin + (c.flex > 0 ? 0 : MeasureStackH(c, Ctx.Measure)) + n.gap;
		}
		const int leftover = FMath::Max(0, (n.h - 2 * n.pad) - fixed);
		int cy = n.y + n.pad;
		for (const auto& cc : n.children)
		{
			UiNode c = cc;  // 子をコピーして配置を上書き（native と同じ）
			cy += c.margin;
			c.x = n.x + n.pad + c.margin;
			if (c.w == 0) c.w = n.w - 2 * n.pad - 2 * c.margin;
			int ch;
			if (c.flex > 0 && sumFlex > 0)
			{
				ch = FMath::Max(c.minH, (int)(leftover * (c.flex / sumFlex) + 0.5f));
				c.h = ch;
			}
			else
			{
				ch = MeasureStackH(c, Ctx.Measure);
				// 高さの無い入れ物の子は、測った高さを持たせる（仕様 §5.1。無いと地と当たりが高さ 0 で描かれた。2026-10-12）
				if (c.h == 0 && (c.type == UiType::VerticalLayout || c.type == UiType::HorizontalLayout || c.type == UiType::GridLayout))
					c.h = ch;
			}
			c.y = cy;
			EmitNode(Ctx, c);
			cy += ch + c.margin + n.gap;
		}
	}

	void EmitHorizontalLayoutNode(FEmitCtx& Ctx, const UiNode& n)
	{
		// VerticalLayout の軸違い。
		EmitLayoutBox(Ctx, n);
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		float sumFlex = 0; int fixed = 0;
		for (const auto& c : n.children)
		{
			sumFlex += c.flex;
			fixed += 2 * c.margin + (c.flex > 0 ? 0 : (c.w > 0 ? c.w : 100)) + n.gap;
		}
		const int leftover = FMath::Max(0, (n.w - 2 * n.pad) - fixed);
		int cx = n.x + n.pad;
		for (const auto& cc : n.children)
		{
			UiNode c = cc;  // 子をコピーして配置を上書き（native と同じ）
			cx += c.margin;
			c.y = n.y + n.pad + c.margin;
			if (c.h == 0) c.h = n.h - 2 * n.pad - 2 * c.margin;
			int cw;
			if (c.flex > 0 && sumFlex > 0)
			{
				cw = FMath::Max(c.minW, (int)(leftover * (c.flex / sumFlex) + 0.5f));
				c.w = cw;
			}
			else
			{
				cw = c.w > 0 ? c.w : 100;
			}
			c.x = cx;
			EmitNode(Ctx, c);
			cx += cw + c.margin + n.gap;
		}
	}

	void EmitTextInputNode(FEmitCtx& Ctx, const UiNode& n)
	{
		const UiTheme& T = *Ctx.Theme;
		// Phase 6: 名前入力欄。沈んだ枠を描き、本文/placeholder/caret と文字編集は Canvas が
		// id 別バッファで担う（TextField コマンド）。幾何は純関数のまま・編集状態は Canvas 保持。
		PushRect(Ctx, n.x, n.y, n.w, n.h, n.hasColor ? n.color : T.track, SkinRad(Ctx, kRadPanel));
		PushOutline(Ctx, n.x, n.y, n.w, n.h, n.focused ? T.accent : T.panelEdge, SkinRad(Ctx, kRadPanel));
		const std::string fieldId = !n.bindVar.empty() ? n.bindVar
		                          : (!n.id.empty() ? n.id : n.action);
		KaedeUI::FKaedeDrawCmd F;
		F.Kind = KaedeUI::EKaedeDrawKind::TextField;
		F.X = (float)(n.x + 12); F.Y = (float)n.y; F.W = (float)(n.w - 24); F.H = (float)n.h;
		F.Sprite = UTF8_TO_TCHAR(fieldId.c_str());
		F.Text = UTF8_TO_TCHAR(n.placeholder.c_str());
		F.Color = ToFColor(n.hasTextColor ? n.textColor : T.textPrimary);
		F.FontPt = (float)n.fontPt;   // 0=キャンバス既定。木の fontSize を欄の文字にも効かせる（2026-09-12）
		F.FocusGroup = Ctx.FocusGroup;   // 当たりと同じ操作スコープ（編集器は最前面の群の欄だけ・2026-10-04）
		F.bEnabled = n.enabled;
		Ctx.Out->Add(F);
		if (n.enabled) AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
	}

	void EmitGridLayoutNode(FEmitCtx& Ctx, const UiNode& n)
	{
		// Phase 6: cols 列のグリッド配置（セル幅は等分・行は各行の最大高で送る）。
		// 幾何ベースなので方向フォーカス（BuildFocusNav）は自動でグリッド遷移になる。
		EmitLayoutBox(Ctx, n);
		if (n.enabled && !n.action.empty())
			AddHit(Ctx, n, (float)n.x, (float)n.y, (float)n.w, (float)n.h, true);
		const int cols = FMath::Max(1, n.cols);
		const int cellW = (n.w - 2 * n.pad - (cols - 1) * n.gap) / cols;
		int cy = n.y + n.pad, col = 0, rowMaxH = 0;
		for (const auto& cc : n.children)
		{
			UiNode c = cc;   // 子をコピーして配置を上書き
			c.x = n.x + n.pad + col * (cellW + n.gap) + c.margin;
			if (c.w == 0) c.w = cellW - 2 * c.margin;
			c.y = cy + c.margin;
			const int ch = c.h > 0 ? c.h : 28;
			c.h = ch;
			EmitNode(Ctx, c);
			rowMaxH = FMath::Max(rowMaxH, ch + 2 * c.margin);
			if (++col >= cols) { col = 0; cy += rowMaxH + n.gap; rowMaxH = 0; }
		}
	}

	void EmitScrollViewNode(FEmitCtx& Ctx, const UiNode& n)
	{
		// Phase 6: 縦スクロールリスト。子を固定高で縦積みし、ClipPush..ClipPop で挟む。
		// 実際のクリップ＋スクロール量ぶんの Y シフトは描画側（SKaedeUiCanvas）が行う
		//（幾何は純関数のまま・スクロール量は id ごとに Canvas が保持）。
		constexpr int kScrollbarW = 10;   // 右端のスクロールバー領域（子幅から差し引く）
		EmitLayoutBox(Ctx, n);

		// 中身の総高さを先に計算（縦積み・固定高。主軸の flex は無効。
		// h 未指定のコンテナは MeasureStackH で中身から導出）。
		int contentH = n.pad;
		for (const auto& c : n.children)
			contentH += c.margin + MeasureStackH(c, Ctx.Measure) + c.margin + n.gap;
		if (!n.children.empty()) contentH -= n.gap;   // 末尾 gap を除く
		contentH += n.pad;
		// スクロールバー領域は「はみ出すときだけ」子幅から差し引く（収まる一覧は従来の
		// VerticalLayout と同じ全幅＝既存 golden 不変。はみ出す時のみバーぶん詰める）。
		const int barW = (contentH > n.h) ? kScrollbarW : 0;

		// スクロールid（未指定は座標から合成。Canvas がスクロール量の保存キーに使う）。
		const std::string sid = !n.id.empty()
			? n.id : ("sv@" + std::to_string(n.x) + "," + std::to_string(n.y));

		KaedeUI::FKaedeDrawCmd Clip;
		Clip.Kind = KaedeUI::EKaedeDrawKind::ClipPush;
		Clip.X = (float)n.x; Clip.Y = (float)n.y; Clip.W = (float)n.w; Clip.H = (float)n.h;
		Clip.Sprite = UTF8_TO_TCHAR(sid.c_str());
		Clip.ContentH = (float)contentH;
		Ctx.Out->Add(Clip);

		const FString SavedScroll = Ctx.ScrollId;
		Ctx.ScrollId = UTF8_TO_TCHAR(sid.c_str());
		int cy = n.y + n.pad;
		for (const auto& cc : n.children)
		{
			UiNode c = cc;   // 子をコピーして配置を上書き
			cy += c.margin;
			c.x = n.x + n.pad + c.margin;
			if (c.w == 0) c.w = n.w - 2 * n.pad - 2 * c.margin - barW;
			const int ch = MeasureStackH(c, Ctx.Measure);
			c.h = ch;
			c.y = cy;
			EmitNode(Ctx, c);
			cy += ch + c.margin + n.gap;
		}
		Ctx.ScrollId = SavedScroll;

		KaedeUI::FKaedeDrawCmd Pop;
		Pop.Kind = KaedeUI::EKaedeDrawKind::ClipPop;
		Ctx.Out->Add(Pop);
	}

	// 部品の型 → 描く関数（1 型 1 関数。再帰は EmitNode 経由）
	void EmitNodeBodyInner(FEmitCtx& Ctx, const UiNode& n)
	{
		switch (n.type)
		{
		case UiType::Polyline:         EmitPolylineNode(Ctx, n); break;
		case UiType::Polygon:          EmitPolygonNode(Ctx, n); break;
		case UiType::Divider:          EmitDividerNode(Ctx, n); break;
		case UiType::Ellipse:          EmitEllipseNode(Ctx, n); break;
		case UiType::Panel:            EmitPanelNode(Ctx, n); break;
		case UiType::Image:            EmitImageNode(Ctx, n); break;
		case UiType::Label:            EmitLabelNode(Ctx, n); break;
		case UiType::Button:           EmitButtonNode(Ctx, n); break;
		case UiType::Slider:           EmitSliderNode(Ctx, n); break;
		case UiType::Toggle:           EmitToggleNode(Ctx, n); break;
		case UiType::ToggleGroup:      EmitToggleGroupNode(Ctx, n); break;
		case UiType::Modal:            EmitModalNode(Ctx, n); break;
		case UiType::VerticalLayout:   EmitVerticalLayoutNode(Ctx, n); break;
		case UiType::HorizontalLayout: EmitHorizontalLayoutNode(Ctx, n); break;
		case UiType::TextInput:        EmitTextInputNode(Ctx, n); break;
		case UiType::GridLayout:       EmitGridLayoutNode(Ctx, n); break;
		case UiType::ScrollView:       EmitScrollViewNode(Ctx, n); break;
		default:
			// ほかの型は子だけ辿る。
			EmitChildren(Ctx, n);
			break;
		}
	}

	// anchor（fill / safe / 各辺 / center）を絶対座標へ解く。
	void ApplyAnchors(UiNode& n, int rx, int ry, int rw, int rh, int vpW, int vpH)
	{
		if (!n.anchor.empty())
		{
			const std::string& a = n.anchor;
			if (a == "fill")        { n.x = 0; n.y = 0; n.w = vpW; n.h = vpH; }
			else if (a == "safe")   { n.x = rx; n.y = ry; n.w = rw; n.h = rh; }
			else if (a == "top")    { n.x = rx; n.y = ry; n.w = rw; }
			else if (a == "bottom") { n.x = rx; n.y = ry + rh - n.h; n.w = rw; }
			else if (a == "left")   { n.x = rx; n.y = ry; n.h = rh; }
			else if (a == "right")  { n.x = rx + rw - n.w; n.y = ry; n.h = rh; }
			else if (a == "center") { n.x = rx + (rw - n.w) / 2; n.y = ry + (rh - n.h) / 2; }
		}
		for (auto& c : n.children) ApplyAnchors(c, rx, ry, rw, rh, vpW, vpH);
	}
}

namespace KaedeUI
{
	// スクロールバーの寸法（宣言は KaedeUiPainter.h）。**描画も当たり判定もここだけを見る。**
	// 掴める幅（HitW）は ScrollView が子から差し引いている幅（kScrollbarW = 10）と同じにする——
	// 描く 6px のままでは細くて掴めないが、10px なら下の要素と取り合いにならない。
	bool ScrollBarGeometry(float VpX, float VpY, float VpW, float VpH,
	                       float ContentH, float Offset, FKaedeScrollBar& Out)
	{
		Out = FKaedeScrollBar{};
		const float MaxScroll = FMath::Max(0.f, ContentH - VpH);
		if (MaxScroll <= 0.f || VpH <= 0.f) { return false; }
		constexpr float kBarW = 6.f, kBarMargin = 2.f, kHitW = 10.f, kThumbMinH = 24.f;
		Out.MaxScroll = MaxScroll;
		Out.TrackW = kBarW;
		Out.TrackX = VpX + VpW - kBarW - kBarMargin;
		Out.TrackY = VpY;
		Out.TrackH = VpH;
		Out.HitW = kHitW;
		Out.HitX = VpX + VpW - kHitW;
		Out.ThumbH = FMath::Clamp(VpH * (VpH / ContentH), kThumbMinH, VpH);
		Out.ThumbY = VpY + FMath::Clamp(Offset / MaxScroll, 0.f, 1.f) * (VpH - Out.ThumbH);
		return true;
	}

	// 折り返しの公開口（宣言は KaedeUiPainter.h）。描画（EmitLabel）と同じ実装を呼ぶ——
	// host が高さを先に決める木（応答パネル等）で、行数の数え方を二重に持たないため。
	TArray<FString> WrapTextToLines(const FString& Text, int32 WrapW, bool bBold,
	                                float FontPt, const FKaedeMeasureFn& Measure)
	{
		if (WrapW <= 0 || Text.IsEmpty()) { return TArray<FString>{ Text }; }
		return WrapToLines(Text, WrapW, bBold, FontPt, Measure);
	}

	int32 WrappedLineHeight(int32 FontPt) { return LineHFor(FontPt); }

	// 半透明の窓どうしが重なった所で、後ろの窓の地を描かない（昔ながらの RPG 制作ツールの窓の重ね方と同じ考え・2026-10-12）。
	// 前の窓（後に積んだ窓）のうち、後ろの窓の中身でないもの（後ろの窓が開いている間に積まれていない）を引く
	void FinalizeKnockout(FEmitCtx& Ctx)
	{
		if (Ctx.Knocks.Num() < 2) return;
		struct FR { int x, y, w, h; };
		for (int32 a = Ctx.Knocks.Num() - 1; a >= 0; --a)
		{
			const FEmitCtx::FKnock& A = Ctx.Knocks[a];
			TArray<FR> Pieces = {{A.x, A.y, A.w, A.h}};
			for (int32 b = a + 1; b < Ctx.Knocks.Num(); ++b)
			{
				const FEmitCtx::FKnock& B = Ctx.Knocks[b];
				if (A.Owner >= 0 && B.Open.Contains(A.Owner)) continue;   // B は A の中身（A が開いている間に積まれた）
				TArray<FR> Next;
				for (const FR& P : Pieces)
				{
					const int x0 = FMath::Max(P.x, B.x), y0 = FMath::Max(P.y, B.y);
					const int x1 = FMath::Min(P.x + P.w, B.x + B.w), y1 = FMath::Min(P.y + P.h, B.y + B.h);
					if (x0 >= x1 || y0 >= y1) { Next.Add(P); continue; }
					if (y0 > P.y) Next.Add({P.x, P.y, P.w, y0 - P.y});
					if (y1 < P.y + P.h) Next.Add({P.x, y1, P.w, P.y + P.h - y1});
					if (x0 > P.x) Next.Add({P.x, y0, x0 - P.x, y1 - y0});
					if (x1 < P.x + P.w) Next.Add({x1, y0, P.x + P.w - x1, y1 - y0});
				}
				Pieces = MoveTemp(Next);
			}
			if (Pieces.Num() == 1 && Pieces[0].w == A.w && Pieces[0].h == A.h) continue;   // 重なり無し
			KaedeUI::FKaedeDrawCmd Base = (*Ctx.Out)[A.CmdIdx];
			Ctx.Out->RemoveAt(A.CmdIdx);
			int32 At = A.CmdIdx;
			for (const FR& P : Pieces)
			{
				KaedeUI::FKaedeDrawCmd C = Base;
				C.X = (float)P.x; C.Y = (float)P.y; C.W = (float)P.w; C.H = (float)P.h;
				C.Radius = (P.w == A.w && P.h == A.h) ? Base.Radius : 0.f;
				Ctx.Out->Insert(C, At++);
			}
		}
	}

	TArray<FKaedeDrawCmd> ComputeDrawList(const UiNode& Root, int32 ViewportW, int32 ViewportH,
	                                      const FKaedeMeasureFn& Measure, TArray<FKaedeHit>* OutHits,
	                                      const UiTheme* ThemeOverride, TArray<FKaedeNodeRect>* OutRects)
	{
		// テーマだけ渡す版＝既定のファミリ（PremiumAtelier）・動き無しのスキン付き版。本体は下の 1 本
		FKaedeUiSkin Skin;
		if (ThemeOverride) Skin.Theme = *ThemeOverride;   // 無ければ既定のテーマ
		return ComputeDrawList(Root, ViewportW, ViewportH, Measure, OutHits, Skin, nullptr, OutRects);
	}

	TArray<FKaedeDrawCmd> ComputeDrawList(const UiNode& Root, int32 ViewportW, int32 ViewportH,
	                                      const FKaedeMeasureFn& Measure, TArray<FKaedeHit>* OutHits,
	                                      const FKaedeUiSkin& Skin, const FKaedeMotionCtx* Motion, TArray<FKaedeNodeRect>* OutRects)
	{
		// スキン付き: テーマ=Skin.Theme、部品規約=Skin.Family（pc98 は直角二重枠へ自動変換）。
		TArray<FKaedeDrawCmd> Out;
		UiNode Resolved = Root;
		ApplyAnchors(Resolved, 0, 0, ViewportW, ViewportH, ViewportW, ViewportH);

		FEmitCtx Ctx;
		Ctx.Out = &Out;
		Ctx.Theme = &Skin.Theme;
		Ctx.Measure = &Measure;
		Ctx.Hits = OutHits;
		Ctx.VpW = ViewportW;
		Ctx.VpH = ViewportH;
		if (OutRects) { OutRects->Reset(); Ctx.Rects = OutRects; }
		Ctx.SkinFamily = Skin.Family;
		if (Motion && Motion->State)
		{
			// beginSync/endSync はホストが（画面+HUD等の複数ツリーを跨いで）括る。
			Ctx.Motion = Motion->State;
			Ctx.MotionScreenId = TCHAR_TO_UTF8(*Motion->ScreenId);
			Ctx.MotionCond = &Motion->EvalCond;
		}
		EmitNode(Ctx, Resolved);
		FinalizeKnockout(Ctx);
		return Out;
	}

	void SeedVarsFromTree(const UiNode& Node, std::unordered_map<std::string, double>& Vars)
	{
		if (!Node.bindVar.empty() && Vars.find(Node.bindVar) == Vars.end())
		{
			double V = 0.0;
			switch (Node.type)
			{
			case UiType::Slider:      V = Node.value; break;
			case UiType::Toggle:      V = Node.on ? 1.0 : 0.0; break;
			case UiType::ToggleGroup: V = (double)Node.selected; break;
			default: break;
			}
			Vars.emplace(Node.bindVar, V);
		}
		for (const auto& C : Node.children) SeedVarsFromTree(C, Vars);
	}

	bool TakePrefix(const FString& In, const TCHAR* Prefix, FString& OutRest)
	{
		if (!In.StartsWith(Prefix)) return false;
		OutRest = In.Mid(FCString::Strlen(Prefix));
		return true;
	}

	bool IsSliderAction(const FString& Action)
	{
		return Action.StartsWith(TEXT("slider:"));
	}

	EKaedeVarAction ApplyShellVarAction(const FString& Action, float RelX, const UiNode& DefaultsTree,
	                                    std::unordered_map<std::string, double>& Vars, FString* OutName)
	{
		FString Rest;
		if (TakePrefix(Action, TEXT("set:"), Rest))
		{
			FString Name, Value;
			if (!Rest.Split(TEXT("="), &Name, &Value)) return EKaedeVarAction::None;
			Vars[TCHAR_TO_UTF8(*Name)] = FCString::Atod(*Value);
			if (OutName) *OutName = Name;
			return EKaedeVarAction::Set;
		}
		if (TakePrefix(Action, TEXT("slider:"), Rest))
		{
			Vars[TCHAR_TO_UTF8(*Rest)] = (double)FMath::Clamp(RelX, 0.f, 1.f);
			if (OutName) *OutName = Rest;
			return EKaedeVarAction::Slider;
		}
		if (TakePrefix(Action, TEXT("toggle:"), Rest))
		{
			const std::string Key = TCHAR_TO_UTF8(*Rest);
			const auto It = Vars.find(Key);
			Vars[Key] = (It != Vars.end() && It->second != 0.0) ? 0.0 : 1.0;
			if (OutName) *OutName = Rest;
			return EKaedeVarAction::Toggle;
		}
		if (Action == TEXT("reset"))
		{
			std::unordered_map<std::string, double> Defaults;
			SeedVarsFromTree(DefaultsTree, Defaults);
			for (const auto& Kv : Defaults) Vars[Kv.first] = Kv.second;
			return EKaedeVarAction::Reset;
		}
		return EKaedeVarAction::None;
	}

	void ApplyVarsToTree(UiNode& Node, const std::unordered_map<std::string, double>& Vars)
	{
		if (!Node.bindVar.empty())
		{
			const auto It = Vars.find(Node.bindVar);
			if (It != Vars.end())
			{
				switch (Node.type)
				{
				case UiType::Slider:      Node.value = (float)It->second; break;
				case UiType::Toggle:      Node.on = It->second != 0.0; break;
				case UiType::ToggleGroup: Node.selected = (int)FMath::RoundToInt(It->second); break;
				default: break;
				}
			}
		}
		for (auto& C : Node.children) ApplyVarsToTree(C, Vars);
	}

	TArray<FKaedeFocusNav> BuildFocusNav(const TArray<FKaedeHit>& Hits)
	{
		TArray<FKaedeFocusNav> Nav;
		Nav.SetNum(Hits.Num());
		constexpr float BIG = 1.0e9f;
		auto Cx = [](const FKaedeHit& H) { return H.X + H.W * 0.5f; };
		auto Cy = [](const FKaedeHit& H) { return H.Y + H.H * 0.5f; };
		for (int32 i = 0; i < Hits.Num(); ++i)
		{
			if (!Hits[i].bFocusable) continue;
			const float ix = Cx(Hits[i]), iy = Cy(Hits[i]);
			float bU = BIG, bD = BIG, bL = BIG, bR = BIG;
			for (int32 j = 0; j < Hits.Num(); ++j)
			{
				if (j == i || !Hits[j].bFocusable || Hits[j].FocusGroup != Hits[i].FocusGroup) continue;
				const float dx = Cx(Hits[j]) - ix, dy = Cy(Hits[j]) - iy;
				if (dy < -1.f) { const float s = -dy + FMath::Abs(dx) * 2.f; if (s < bU) { bU = s; Nav[i].Up = j; } }
				if (dy >  1.f) { const float s =  dy + FMath::Abs(dx) * 2.f; if (s < bD) { bD = s; Nav[i].Down = j; } }
				if (dx < -1.f) { const float s = -dx + FMath::Abs(dy) * 2.f; if (s < bL) { bL = s; Nav[i].Left = j; } }
				if (dx >  1.f) { const float s =  dx + FMath::Abs(dy) * 2.f; if (s < bR) { bR = s; Nav[i].Right = j; } }
			}
		}
		return Nav;
	}

	FString HitKey(const FKaedeHit& Hit)
	{
		return Hit.FocusId.IsEmpty() ? (TEXT("action:") + Hit.Action) : (TEXT("id:") + Hit.FocusId);
	}

	int32 MeasureNodeHeight(const UiNode& Node, const FKaedeMeasureFn& Measure)
	{
		return MeasureStackH(Node, &Measure);
	}

	int32 SetActionEnabled(UiNode& Node, const FString& Action, bool bEnabled)
	{
		int32 Count = 0;
		if (!Node.action.empty() && Action == UTF8_TO_TCHAR(Node.action.c_str()))
		{
			Node.enabled = bEnabled;
			++Count;
		}
		for (auto& C : Node.children) Count += SetActionEnabled(C, Action, bEnabled);
		return Count;
	}
}
