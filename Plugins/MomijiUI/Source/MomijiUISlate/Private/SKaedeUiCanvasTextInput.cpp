// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// SKaedeUiCanvasTextInput.cpp — SKaedeUiCanvas の文字欄（TextInput・Phase 6。IME 対応は 2026-08-10）。
//
// 編集は ChildSlot の SEditableText に任せる。自前でキーを溜める方式では IME を通らず
// 日本語が打てなかった（名前入力欄がある作品を出せない）。UE で IME を自前対応するには
// ITextInputMethodContext の実装が要り、変換範囲・キャレット・選択・候補ウィンドウ位置まで
// 面倒を見ることになる＝ SEditableText の再実装。だから本物を1個だけ重ねる。
// 2026-10-18 に SKaedeUiCanvas.cpp から分けた（Issue #73）。
#include "SKaedeUiCanvas.h"
#include "KaedeUiCanvasDraw.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"   // TextInput の実体（IME・選択・クリップボードのため）

DEFINE_LOG_CATEGORY_STATIC(LogKaedeUiTextInput, Log, All);

void SKaedeUiCanvas::Construct(const FArguments& InArgs)
{
	LogicalW = FMath::Max(1, InArgs._LogicalW);
	LogicalH = FMath::Max(1, InArgs._LogicalH);
	FontSize = FMath::Max(1, InArgs._FontSize);
	FontFamily = InArgs._FontFamily;

	ChildSlot
	[
		SAssignNew(TextEditor, SEditableText)
		// 編集中のフィールドが無い間は畳む＝レイアウトにも当たり判定にも出てこない。
		.Visibility_Lambda([this]()
		{
			return ActiveTextField.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
		})
		// 枠と背景はキャンバスが描いているので、ここは文字だけ。倍率は最後に描いたときの値。
		.Font_Lambda([this]()
		{
			FVector2f P, S; float Px = (float)FontSize;
			TextFieldRect(ActiveTextField, P, S, &Px);
			return KaedeUI::MakeKaedeUiFont(false,
				FMath::Max(1, FMath::RoundToInt(Px * PaintedScale())), FontFamily);
		})
		.ColorAndOpacity(FLinearColor(0.92f, 0.90f, 0.85f, 1.f))
		// 空のときの例文。非編集時にキャンバスが描く placeholder（TextField の C.Text）と同じ文を出す。
		// 渡さないと、入力欄に焦点がある間（＝本編ではほぼ常に）例文が消えていた（2026-10-03 にブラウザ版と並べて発覚）
		.HintText_Lambda([this]()
		{
			for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
			{
				if (C.Kind == KaedeUI::EKaedeDrawKind::TextField && C.Sprite == ActiveTextField) { return FText::FromString(C.Text); }
			}
			return FText::GetEmpty();
		})
		.OnTextChanged_Lambda([this](const FText& T) { OnEditorTextChanged(T); })
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type Type)
		{
			// Enter/フォーカス喪失＝確定、Escape＝取消。どちらも値は残す（従来の挙動）。
			if (!ActiveTextField.IsEmpty()) { TextBuffers.Add(ActiveTextField, T.ToString()); }
			if (Type == ETextCommit::OnEnter || Type == ETextCommit::OnCleared)
			{
				CommitTextField(/*bEnter=*/Type == ETextCommit::OnEnter);
			}
		})
	];
}

// Enter / Escape の確定。**送信する欄（action つき）は畳まない。**
//
// 2026-09-20 まではどちらでも `ActiveTextField` を落としていた。SEditableText の Visibility は
// それだけを見ているので、Enter のたびに Collapsed → Visible を往復する。畳んだ瞬間に IME の
// 入力コンテキストが降り、外殻が戻す `SetKeyboardFocus` は**まだ配置されていない（Collapsed の）**
// ウィジェットに対して成功する——ログは ok=1 なのに日本語が打てない、という形になっていた
// （実測 153 回すべて ok=1・記録 Docs/Reports/2026/09/20/04・設計 同 05 §1）。
// 往復そのものを無くすのが直し方なので、**欄を生かしたまま action だけ発火する**。
void SKaedeUiCanvas::CommitTextField(bool bEnter)
{
	const FString Field = ActiveTextField;
	if (Field.IsEmpty()) { return; }
	// 送る直前にもう一度確かめる: 操作できない欄（Modal の背面・押せない・隠れた）からは送らず、編集を降りる（2026-10-04 F1）
	if (!IsTextInputInteractive(Field))
	{
		LeaveTextEditor();
		return;
	}

	// その欄（TextInput ノード）の action（2026-09-11。「Enter で送信」＝設計 08/29/03 §5.1）。
	// 欄の当たり判定（AddHit）が action を持っているので、欄の中心で hit を引く。
	FString Action;
	if (bEnter)
	{
		// 欄の**見える部分**の中心で引く（半分だけ見える欄は枠の中心が見える範囲の外にあり、当たりが取れなかった・2026-10-04）
		FVector2f P, S;
		if (TextInputVisibleRect(Field, P, S))
		{
			const int32 Idx = HitAtLogical(FVector2f(P.X + S.X * 0.5f, P.Y + S.Y * 0.5f));
			if (Idx >= 0 && !Hits[Idx].Action.IsEmpty())
			{
				Action = Hits[Idx].Action;   // OnAction が SetContent で Hits を差し替えるので値で持つ
			}
		}
	}

	// 送信しない確定（Escape・action の無い名前入力欄など）は従来どおり編集を抜ける。
	// 値は TextBuffers に残るので、次に入り直せば戻る。
	if (Action.IsEmpty())
	{
		ActiveTextField.Reset();
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		return;
	}
	// 中身は外殻が空にする（`SetTextInputValue` → `SyncTextEditor(false)` はフォーカスを奪わない）。
	// 送信欄を「打ったら消えて、そのまま次を打てる」形にするのは、チャット欄の定石と同じ。
	if (OnAction) { OnAction(Action, 0.f); }
}

void SKaedeUiCanvas::LeaveTextEditor()
{
	if (ActiveTextField.IsEmpty()) { return; }
	ActiveTextField.Reset();
	SyncTextEditor(/*bTakeFocus=*/false);
}

TArray<FString> SKaedeUiCanvas::TextInputIds() const
{
	TArray<FString> Out;
	for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
	{
		if (C.Kind == KaedeUI::EKaedeDrawKind::TextField) { Out.AddUnique(C.Sprite); }
	}
	return Out;
}

bool SKaedeUiCanvas::HasTextInput(const FString& Id) const
{
	if (Id.IsEmpty()) { return false; }
	for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
	{
		if (C.Kind == KaedeUI::EKaedeDrawKind::TextField && C.Sprite == Id) { return true; }
	}
	return false;
}

// 操作できなくなった編集欄を降ろす。SetContent とスクロールのたびに呼ぶ。
// 消えた欄（手帳・図の面や「文字を消す」で TextInput が prune される）だけでなく、Modal が開いて背面になった欄・
// 押せなくなった欄・ScrollView で完全に隠れた欄も降りる（2026-10-04・Codex 10/04/01 F1/F2）。
// これが無いと、SEditableText が立ち続けて、確認ダイアログの背面から Enter で送信できた。
void SKaedeUiCanvas::ReconcileTextEditor()
{
	if (ActiveTextField.IsEmpty() || IsTextInputInteractive(ActiveTextField)) { return; }
	ActiveTextField.Reset();
	// 編集器がキーボードフォーカスを持っていたら、キャンバスへ移す（打鍵が降りた欄へ流れないように。Modal の中の操作は方向キーで続けられる）
	if (TextEditor.IsValid() && FSlateApplication::IsInitialized() && TextEditor->HasKeyboardFocus())
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
	SyncTextEditor(/*bTakeFocus=*/false);
}

void SKaedeUiCanvas::OnEditorTextChanged(const FText& T)
{
	if (!ActiveTextField.IsEmpty()) { TextBuffers.Add(ActiveTextField, T.ToString()); }
}

bool SKaedeUiCanvas::TypeIntoActiveTextInput(const FString& Text)
{
	if (ActiveTextField.IsEmpty() || !TextEditor.IsValid()) { return false; }
	const FText T = FText::FromString(GetTextInputValue(ActiveTextField) + Text);
	TextEditor->SetText(T);
	OnEditorTextChanged(T);
	return true;
}

bool SKaedeUiCanvas::TextFieldClip(const FString& Id, FVector4f& OutVp) const
{
	bool bFound = false;
	WalkCmds(/*bWithGhosts=*/false, [&](int32, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)
	{
		if (C.Kind != KaedeUI::EKaedeDrawKind::TextField || C.Sprite != Id) return true;
		if (Clip && !Clip->Sprite.IsEmpty())
		{
			if (const FVector4f* Vp = ScrollViewports.Find(Clip->Sprite)) { OutVp = *Vp; bFound = true; }
		}
		return false;
	});
	return bFound;
}

bool SKaedeUiCanvas::TextInputVisibleRect(const FString& Id, FVector2f& OutPos, FVector2f& OutSize) const
{
	FVector2f P, S;
	if (!TextFieldRect(Id, P, S)) return false;
	float X0 = P.X - 12.f, Y0 = P.Y, X1 = P.X + S.X + 12.f, Y1 = P.Y + S.Y;   // テキスト領域から枠全体を復元
	FVector4f Vp;
	if (TextFieldClip(Id, Vp))
	{
		X0 = FMath::Max(X0, Vp.X); Y0 = FMath::Max(Y0, Vp.Y);
		X1 = FMath::Min(X1, Vp.X + Vp.Z); Y1 = FMath::Min(Y1, Vp.Y + Vp.W);
	}
	if (X1 <= X0 || Y1 <= Y0) return false;
	OutPos = FVector2f(X0, Y0); OutSize = FVector2f(X1 - X0, Y1 - Y0);
	return true;
}

bool SKaedeUiCanvas::IsTextInputInteractive(const FString& Id) const
{
	if (Id.IsEmpty()) return false;
	const int32 Top = TopFocusGroup();
	for (const KaedeUI::FKaedeDrawCmd& C : Cmds)
	{
		if (C.Kind != KaedeUI::EKaedeDrawKind::TextField || C.Sprite != Id) continue;
		if (!C.bEnabled || C.FocusGroup != Top) return false;
		FVector2f P, S;
		return TextInputVisibleRect(Id, P, S);
	}
	return false;
}

void SKaedeUiCanvas::SyncTextEditor(bool bTakeFocus)
{
	if (!TextEditor.IsValid())
	{
		return;
	}
	if (ActiveTextField.IsEmpty())
	{
		TextEditor->SetText(FText::GetEmpty());
	}
	else
	{
		TextEditor->SetText(FText::FromString(GetTextInputValue(ActiveTextField)));
		if (bTakeFocus && FSlateApplication::IsInitialized())
		{
			// 戻り値を見る（2026-09-12 スモーク: 送信や picker のあとで IME が ON にならない件の原因取り）。
			// 直前まで畳まれていた（Collapsed）SEditableText は、この時点ではまだ配置されていないので
			// フォーカスの経路が引けず失敗することがある。失敗したら次のフレームでもう一度だけ試す。
			const bool bOk = FSlateApplication::Get().SetKeyboardFocus(TextEditor, EFocusCause::Mouse);
			UE_LOG(LogKaedeUiTextInput, Display, TEXT("[ui] textfield focus id=%s ok=%d"), *ActiveTextField, bOk ? 1 : 0);
			if (!bOk)
			{
				const FString WantId = ActiveTextField;
				RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this, WantId](double, float)
				{
					if (ActiveTextField == WantId && TextEditor.IsValid() && FSlateApplication::IsInitialized() && !TextEditor->HasKeyboardFocus())
					{
						const bool bRetry = FSlateApplication::Get().SetKeyboardFocus(TextEditor, EFocusCause::Mouse);
						UE_LOG(LogKaedeUiTextInput, Display, TEXT("[ui] textfield focus retry id=%s ok=%d"), *WantId, bRetry ? 1 : 0);
					}
					return EActiveTimerReturnType::Stop;
				}));
			}
		}
	}
	// 表示/非表示とフォントが変わるのでレイアウトから作り直す。
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

bool SKaedeUiCanvas::IsTextEditorFocused() const
{
	return TextEditor.IsValid() && TextEditor->HasKeyboardFocus();
}

bool SKaedeUiCanvas::SetActiveTextInput(const FString& Id)
{
	if (!Id.IsEmpty() && !IsTextInputInteractive(Id))
	{
		UE_LOG(LogKaedeUiTextInput, Display, TEXT("[ui] textfield focus refused id=%s (not interactive)"), *Id);
		return false;
	}
	ActiveTextField = Id;
	SyncTextEditor(/*bTakeFocus=*/true);
	return !Id.IsEmpty();
}

void SKaedeUiCanvas::SetTextInputValue(const FString& Id, const FString& Value)
{
	TextBuffers.Add(Id, Value);
	// 編集中のフィールドを外から書き換えたら、編集ウィジェットにも反映する
	// （さもないと次のキー入力で古い内容が書き戻る）。
	if (!ActiveTextField.IsEmpty() && ActiveTextField == Id)
	{
		SyncTextEditor(/*bTakeFocus=*/false);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

// 描画コマンド列から id の TextField の論理矩形を拾う（TextFieldAt と同じスクロール補正）。
bool SKaedeUiCanvas::TextFieldRect(const FString& Id, FVector2f& OutPos, FVector2f& OutSize, float* OutFontPt) const
{
	if (Id.IsEmpty())
	{
		return false;
	}
	bool bFound = false;
	WalkCmds(/*bWithGhosts=*/false, [&](int32, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)
	{
		if (C.Kind != KaedeUI::EKaedeDrawKind::TextField || C.Sprite != Id) return true;
		const bool bScrolled = Clip && !Clip->Sprite.IsEmpty();
		OutPos = FVector2f(C.X, C.Y - (bScrolled ? ScrollOffsetFor(Clip->Sprite) : 0.f));
		OutSize = FVector2f(C.W, C.H);
		if (OutFontPt) *OutFontPt = C.FontPt > 0.f ? C.FontPt : (float)FontSize;
		bFound = true;
		return false;
	});
	return bFound;
}

void SKaedeUiCanvas::OnArrangeChildren(const FGeometry& AllottedGeometry,
	FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget> Child = ChildSlot.GetWidget();
	if (!ArrangedChildren.Accepts(Child->GetVisibility()))
	{
		return;
	}
	FVector2f P, S; float FieldPx = (float)FontSize;
	if (!TextFieldRect(ActiveTextField, P, S, &FieldPx))
	{
		return;
	}
	// OnPaint と同じレターボックス（LetterboxFor）。ここがズレると入力欄が枠から外れる。
	const FLetterbox LB = LetterboxFor(AllottedGeometry.GetLocalSize());
	const float Scale = LB.Scale;

	// **縦は枠いっぱいにしない。** SEditableText は与えられた高さの上端から描くので、
	// 枠(48px)をそのまま渡すと文字が上へ寄る（2026-08-10 に実機で発覚）。
	// 非編集時の描画（DrawKaedeTextField）と**同じ式**で1行ぶんの帯を置く＝
	// クリックして編集に入っても文字が動かない。
	const FSlateFontInfo Font = KaedeUI::MakeKaedeUiFont(false,
		FMath::Max(1, FMath::RoundToInt(FieldPx * Scale)), FontFamily);
	float LineH = FieldPx * Scale;
	if (FSlateApplication::IsInitialized())
	{
		// 文字によらず一定の高さを使う（Measure(text) だと打つたびに行位置が動く）
		LineH = (float)FSlateApplication::Get().GetRenderer()
			->GetFontMeasureService()->GetMaxCharacterHeight(Font);
	}
	// 上端は非編集時の描画と同じく基準の全角 1 字の字面で決める。
	// SEditableText はこの上端から行の高さ LineH で描くので、字の位置が非編集時と一致する。
	const float TextY = KaedeUI::KaedeInkCenteredTextPos(TEXT("あ"), Font,
		FVector2f(0.f, LB.OffY + P.Y * Scale), S.Y * Scale, false).Y;

	ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(
		Child,
		FVector2f(LB.OffX + P.X * Scale, TextY),
		FVector2f(S.X * Scale, LineH)));
}

FString SKaedeUiCanvas::TextFieldAt(FVector2f L) const
{
	// ScrollView 配下はスクロール量ぶん Y がずれ、ビューポート外はクリップされる。
	// 描画（OnPaint）と同じ ClipPush/ClipPop 追跡（WalkCmds）で当たり判定を一致させる
	//（Codexレビュー Docs/CodexReports/2026/07/14/01 §注意点。HitAtLogical と同じ規約）。
	FString Out;
	const int32 Top = TopFocusGroup();
	WalkCmds(/*bWithGhosts=*/false, [&](int32, const KaedeUI::FKaedeDrawCmd& C, const KaedeUI::FKaedeDrawCmd* Clip)
	{
		if (C.Kind != KaedeUI::EKaedeDrawKind::TextField) return true;
		// 操作できない欄（押せない・Modal の背面）は当たらない＝Modal の暗幕が受ける（2026-10-04 F1）
		if (!C.bEnabled || C.FocusGroup != Top) return true;
		float cy = C.Y;
		if (Clip && !Clip->Sprite.IsEmpty())
		{
			cy -= ScrollOffsetFor(Clip->Sprite);
			if (L.Y < Clip->Y || L.Y >= Clip->Y + Clip->H) return true;   // ビューポート外
		}
		const float fx = C.X - 12.f, fw = C.W + 24.f;   // テキスト領域から枠全体を復元
		if (L.X >= fx && L.X < fx + fw && L.Y >= cy && L.Y < cy + C.H) { Out = C.Sprite; return false; }
		return true;
	});
	return Out;
}

// 最後に ChildSlot（編集中の TextInput）。フォーカスリングより上＝入力欄が隠れない。
// 編集していない間は Collapsed なので、この呼び出しは何も描かない。
// 欄が ScrollView の中なら、その見える範囲で SEditableText を切る（描画のクリップ＋当たりの範囲。Slate は描いたときのクリップで当たりを判定する）。
// これが無いと、半分だけ見える欄の枠外が描かれ、押せた（2026-10-04・Codex 10/04/01 F2）
int32 SKaedeUiCanvas::PaintTextEditor(const FPaintArgs& Args, const KaedeUI::FKaedePaintCtx& P, const FSlateRect& MyCullingRect,
	int32 Layer, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FVector4f EditorVp;
	const bool bClipEditor = !ActiveTextField.IsEmpty() && TextFieldClip(ActiveTextField, EditorVp);
	FSlateRect ChildCull = MyCullingRect;
	if (bClipEditor)
	{
		P.PushClip(EditorVp.X, EditorVp.Y, EditorVp.Z, EditorVp.W);
		const FVector2f A = P.Geo.LocalToAbsolute(P.ToLocal(EditorVp.X, EditorVp.Y));
		const FVector2f B = P.Geo.LocalToAbsolute(P.ToLocal(EditorVp.X + EditorVp.Z, EditorVp.Y + EditorVp.W));
		ChildCull = MyCullingRect.IntersectionWith(FSlateRect(A.X, A.Y, B.X, B.Y));
	}
	const int32 OutLayer = SCompoundWidget::OnPaint(Args, P.Geo, ChildCull, P.Out, Layer + 2, InWidgetStyle, bParentEnabled);
	if (bClipEditor) P.Out.PopClip();
	return FMath::Max(OutLayer, Layer + 3);   // Layer+3 は「コピーしました」の文字
}
