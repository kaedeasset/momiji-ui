# MomijiUI 導入ガイド

この説明書は MomijiUI 0.1.0（2026-10-21）のものです。

画面を JSON で書き、Unreal Engine（UE）とブラウザで同じ配置・同じ操作で描く、無料の UI フレームワークです。このガイドは、Fab で入れてから見本のカウンターを UE で動かし、自分の画面を 1 つ書いて検査するまでの手順です。

![同じ設定の画面 JSON を、左は UE（Slate）、右はブラウザ（canvas）で描いた所](images/momiji-ui/same-ue-web.png)

- 部品・鍵・列挙・動き・テーマ・診断のコードを 1 つずつ引く説明は、[リファレンス](momiji-ui-reference.md)にあります。
- 版ごとに何が変わったかは、[更新履歴](momiji-ui-changelog.md)にあります。
- [English](momiji-ui-guide.en.md)

---

## 目次

1. [はじめての一本道（入れてから検査するまで）](#1-はじめての一本道入れてから検査するまで)
2. [このプラグインでできること](#2-このプラグインでできること)
3. [要るもの](#3-要るもの)
4. [あなたのプログラムの仕事（4 つ）](#4-あなたのプログラムの仕事4-つ)
5. [UE で使う](#5-ue-で使う)
6. [ブラウザで使う](#6-ブラウザで使う)
7. [UE とブラウザで違う所](#7-ue-とブラウザで違う所)
8. [まだできないこと](#8-まだできないこと)
9. [困ったとき](#9-困ったとき)

---

## 1. はじめての一本道（入れてから検査するまで）

UE で MomijiUI を初めて使う人が、Fab で入れてから、自分の画面を 1 つ書いて検査するまでの順番です。上から順に進めれば、ほかの章を読まずに最後まで行けます。ほかの章は、自分のゲームに組み込むときに、必要な所だけ読んでください。

**先に全体の順番です。** MomijiUI は単独で起動するアプリではなく、あなたの UE のプロジェクトに入れて、C++ から使う部品です。エディタのメニューや Blueprint の部品はありません。このガイドでは、プログラムを立ち上げることを「起動する」（ランチャー・UE）、動いているプログラムの中で窓や画面を出すことを「開く」「出す」と書き分けます。

| 順 | やること | どこで | 終わると |
|---|---|---|---|
| 1.1 | Fab で MomijiUI を入れる | Fab とランチャー | UE に MomijiUI が入る |
| 1.2 | UE 5.8 と Visual Studio を入れる | ランチャーと Microsoft のインストーラー | C++ のプロジェクトを作る準備が整う |
| 1.3 | C++ のプロジェクトを作り、MomijiUI を有効にする | UE のプロジェクトブラウザとエディタ | MomijiUI の入ったプロジェクトが開く |
| 1.4 | 見本のカウンターを UE で動かす | UE のエディタ（プレイ） | 押すと数が増える画面が出る |
| 1.5 | 同じ JSON がブラウザで動くのを見る | ブラウザ | 同じ画面がブラウザでも動く |
| 1.6 | 自分の画面を 1 つ書いて出す | テキストエディタと UE | 自分の画面の絵が撮れる |
| 1.7 | 画面 JSON を検査する | Visual Studio と UE のエディタ | 誤りが場所つきで出る |

2 回目からは、1.3 で作ったプロジェクトを開くだけです。1.1〜1.3 は最初の 1 回だけです。

### 1.1 Fab で MomijiUI を入れる

1. Epic Games のアカウントで Fab（fab.com）にサインインし、MomijiUI のページで「ライブラリに追加」します。無料です。
2. Epic Games Launcher（ランチャー）の **Unreal Engine → ライブラリ** の Fab の欄に MomijiUI が並びます。**エンジンにインストール** を押し、**5.8** を選びます。プラグインは `<UE のフォルダ>\Engine\Plugins\Marketplace\` の下の MomijiUI のフォルダ（`MomijiUI.uplugin` のあるフォルダ）に入ります。このフォルダを、以下「プラグインのフォルダ」と書きます。

UE 5.8 がまだ入っていなければ、先に 1.2 の手順 1 を済ませてから戻ってください。

### 1.2 UE 5.8 と Visual Studio を入れる

1. ランチャーの **Unreal Engine → ライブラリ** で、エンジンのバージョンの **＋** を押し、**5.8** を選んで **インストール** します。
2. Microsoft のサイトから **Visual Studio Community**（無料）のインストーラーを取ってきて起動し、ワークロードの **C++ によるゲーム開発** に印を付けて入れます。版は **2022 の最新（17.14）か 2026 の最新**にします（UE 5.8 は、古い版の一部〔2022 の 17.13 以前・2026 の初めの版〕を組み立てに使いません。もう入っているなら Visual Studio Installer の **更新** を押します）。MomijiUI は C++ から使うので、プロジェクトを C++ で作ります（1.3）。そのために要ります。

### 1.3 C++ のプロジェクトを作り、MomijiUI を有効にする

1. ランチャーで 5.8 の **起動** を押します。
2. プロジェクトブラウザで **ゲーム → 空のプロジェクト**（Blank）を選び、**C++** を選びます。プロジェクトの名前は**英数字と `_` だけ**（例 `MyUi`）にして **作成** を押します。Visual Studio がプロジェクトのコードを組み立て、エディタが開きます。
3. エディタの **編集 → プラグイン** で **MomijiUI** を探し、印を付けて有効にします。MomijiUI はいまベータ版なので、ベータ版の注意の窓が出ます。**はい** を押して続けます。
4. 「再起動が必要」と出るので、エディタを開き直します。同じプロジェクトを開きます。
5. エディタを閉じます（1.4 で、引数をつけて起動し直すため）。

プラグインはエンジンに入っただけでは使えず、プロジェクトごとに有効にします。有効にするのは、プロジェクトを作った最初の 1 回だけです。

### 1.4 見本のカウンターを UE で動かす

見本は「押した回数」と「名前の一覧」の小さな画面です。画面の形はプラグインのフォルダの `Examples/counter.json`、動かすコードはプラグインに入っている見本のウィジェット（`SMomijiUICounterExample`）です。起動引数 `-KaedeUiExample=counter` をつけてエディタを起動すると、プレイを始めたときにその画面がゲームの画面の上に出ます。

1. PowerShell を開き、次の 1 行を打ちます（UE の場所とプロジェクトの場所は、あなたのものに替えます。UE の場所は、ランチャーの **Unreal Engine → ライブラリ** で 5.8 の **起動** の横の **▼ → オプション** に出るインストール先です。その中の `Engine\Binaries\Win64\UnrealEditor.exe` を使います）。
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MyUi\MyUi.uproject" -KaedeUiExample=counter
   ```
2. エディタが開いたら、上の段の **▶（プレイ）** を押します。ゲームの画面の上に、見本のカウンターが出ます。
3. **+1** を押すと回数が増えます。1 回以上で **リセット** が押せるようになり、5 回で「5 回押した」が出ます。下の欄に名前を打って Enter（か **追加**）を押すと、一覧に足されます。日本語入力も使えます。
4. 部品の外を押してマウスの矢印が消えたら、**Shift+F1** で戻ります（ゲームがマウスを取ったため）。**Esc** でプレイを止めます。

見本の画面が出るのは、その起動の最初のプレイの 1 回だけです。もう一度見るときは、エディタを閉じて 1. からやり直します。

### 1.5 同じ JSON がブラウザで動くのを見る

MomijiUI には、同じ画面 JSON をブラウザの `<canvas>` に描くブラウザ版があります。

1. ブラウザで説明書の Web のトップ <https://momiji.kaedeasset.com/> を開きます。
2. ページの中に、1.4 と同じ `counter.json` を描いたカウンターが動いています。**+1**・**リセット**・名前の欄が、1.4 と同じ位置で同じように動くことを確かめます。

![見本のカウンター（ブラウザ。回数 3・名前 3 つの所）](images/momiji-ui/counter.png)

ブラウザ版の配布物（自分の HTML から読むファイル）は、GitHub の Releases から取れます（§6）。

### 1.6 自分の画面を 1 つ書いて出す

文字（`Label`）とボタン（`Button`）の 2 つを持つ画面を書き、UE で絵を 1 枚撮ります。

1. プロジェクトのフォルダ（`MyUi.uproject` のあるフォルダ）に `UI` というフォルダを作り、その中に `hello.json` を次の中身で作ります。文字コードは UTF-8 にします。
   ```json
   {
    "bind": { "strings": { "name": "MomijiUI" } },
    "root": {
     "type": "VerticalLayout", "anchor": "center", "w": 400, "h": 200, "pad": 20, "gap": 12,
     "children": [
      { "type": "Label", "text": "こんにちは {name}", "h": 32, "fontSize": 20, "align": "center" },
      { "type": "Button", "text": "OK", "action": "ok", "h": 44 }
     ]
    }
   }
   ```
   - `root` の中が画面です。部品は `type` で決まり、`children` で入れ子にします。
   - 画面は 1280×720 として書きます。`anchor: "center"` で真ん中に寄せます。
   - `{name}` は描くときに渡す値に置き換わります。ここでは `bind` に書いた値（`MomijiUI`）で描きます。
   - `action` は、押されたときにあなたのプログラムへ届く文字列です。
2. エディタを閉じ、PowerShell で次の 1 行を打ちます。
   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MyUi\MyUi.uproject" -game -ResX=1280 -ResY=720 -KaedeUiShot=UI/hello.json
   ```
   ゲームの窓が開き、自分の画面を描いて 1 枚撮り、窓が閉じます。新しいプロジェクトの最初の起動は、窓が出るまでに 1 分ほどかかることがあります（シェーダーの準備）。窓が閉じるまで待ちます。
3. プロジェクトのフォルダの `Saved\Screenshots\WindowsEditor\MomijiUI.png` を開きます。真ん中の板に「こんにちは MomijiUI」と **OK** のボタンが写っていれば、書けています。

`-KaedeUiShot` に渡す場所は、プロジェクトのフォルダからの相対か、絶対の場所です。

### 1.7 画面 JSON を検査する

書き間違い（知らない部品・綴り違いの鍵・型違い・同じ id）は、描く前に MomijiUI の検査（診断）で見つけます。検査はプラグインの関数 `KaedeCore::ValidateScreenJson` です。エディタの下の欄から呼べるように、プロジェクトに 1 つファイルを足します。

1. Visual Studio で、1.3 で作られた `MyUi.sln`（プロジェクトのフォルダにあります）を開きます。
2. `<プロジェクトのフォルダ>\Source\MyUi\MyUi.Build.cs` の `PublicDependencyModuleNames` の行に `"MomijiUICore"` を足します。
   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "MomijiUICore" });
   ```
   （`{ … }` の中は、あなたのファイルに元からある名前のまま、最後に `"MomijiUICore"` を足します。）
3. 同じフォルダ（`<プロジェクトのフォルダ>\Source\MyUi\`）に、エクスプローラーかメモ帳で `MomijiCheck.cpp` を作り、次の中身を貼って **UTF-8** で保存します（Visual Studio の「新しい項目の追加」は既定の置き場が `Intermediate` の下になることがあり、そこに置くと組み立てに入りません）。
   ```cpp
   #include "CoreMinimal.h"
   #include "HAL/IConsoleManager.h"
   #include "Misc/FileHelper.h"
   #include "Misc/Paths.h"
   #include "MomijiUICore.h"

   // エディタの下の欄で「Momiji.Check UI/hello.json」と打つと、画面 JSON の診断を出力ログに出す
   static FAutoConsoleCommand GMomijiCheck(
   	TEXT("Momiji.Check"),
   	TEXT("Momiji.Check <画面 JSON（プロジェクトのフォルダからの相対）>"),
   	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
   	{
   		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("Momiji.Check <画面 JSON>")); return; }
   		FString Json;
   		if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir() / Args[0])))
   		{
   			UE_LOG(LogTemp, Error, TEXT("読めない: %s"), *Args[0]);
   			return;
   		}
   		const auto Diags = KaedeCore::ValidateScreenJson(TCHAR_TO_UTF8(*Json));
   		for (const kaedevn::ui::UiDiagnostic& D : Diags)
   		{
   			UE_LOG(LogTemp, Warning, TEXT("%s %s %s %s"), UTF8_TO_TCHAR(D.severity.c_str()), UTF8_TO_TCHAR(D.code.c_str()),
   				UTF8_TO_TCHAR(D.path.c_str()), UTF8_TO_TCHAR(D.message.c_str()));
   		}
   		UE_LOG(LogTemp, Display, TEXT("Momiji.Check: %s の診断は %d 件"), *Args[0], (int32)Diags.size());
   	}));
   ```
4. Visual Studio の上の段で構成が **Development Editor**・**Win64** になっていることを確かめ、**ビルド → ソリューションのビルド** を押します。最後に「1 正常終了、0 失敗」と出れば組み立てられています。
5. `MyUi.uproject` を開いてエディタを起動し、**ウィンドウ → 出力ログ** を開きます。下の欄（**Cmd**）に `Momiji.Check UI/hello.json` と打って Enter を押します。出力ログに「Momiji.Check: UI/hello.json の診断は 0 件」と出れば、誤りはありません。
6. 誤りが見つかる様子も見ます。`hello.json` の `"Label"` を `"Labl"` に書き換えて保存し、もう一度 `Momiji.Check UI/hello.json` を打ちます。`error unknown-type /root/children/0/type …` の 1 行が出ます（重さ・コード・場所・説明の順）。`"Label"` に戻して保存すると、0 件に戻ります。

![検査が返すもの。左は誤りを 5 つ入れた画面 JSON、右はその診断（重さ・コード・場所・説明）。ブラウザの検査の結果を画面に描いたもの](images/momiji-ui/diagnostics.png)

ここまでで一本道は終わりです。自分のゲームに組み込むときは §4 と §5 を読んでください。部品と鍵の全部を引く先は、このガイドの冒頭にあります。

---

## 2. このプラグインでできること

- **画面は JSON。** 部品・大きさ・色・字の大きさ・並び・条件を JSON に書きます。あなたのプログラムは値（文字・数・一覧）を渡し、押された動作を受け取るだけです。画面の形を変えるのに、コードを組み立て直す必要はありません。
- **UE とブラウザで同じ配置と同じ操作。** 同じ画面 JSON と同じ値から、UE（Slate）とブラウザ（`<canvas>`）が同じ位置・同じ押せる場所・同じ焦点の動きになります。プラグインに入っている照合の試料 19 枚（`Fixtures/`）で、2 つを突き合わせて確かめています（[リファレンス](momiji-ui-reference.md) §11）。
- **部品 17 種。** 板（`Panel`）・画像（`Image`）・文字（`Label`）・ボタン（`Button`）・スライダー（`Slider`）・トグル（`Toggle`・`ToggleGroup`）・スクロール（`ScrollView`）・モーダル（`Modal`）・縦／横／格子に並べる器（`VerticalLayout`・`HorizontalLayout`・`GridLayout`）・1 行の文字欄（`TextInput`）・折れ線（`Polyline`）・楕円（`Ellipse`）・多角形（`Polygon`）・区切り線（`Divider`）。
- **配置。** 縦・横に積む・残りを `flex` で分ける・格子・スクロール・画面の端や真ん中に寄せる `anchor`（7 つ）。高さを書かなくても、中身や折り返しの行数から決まります。
- **値の差し込み。** 文字（`strings`）・数（`vars`）・一覧（`lists`）を描くたびに渡します。`{鍵}` の置き換え・一覧を並べる `repeat` と `template`（要素の種類で形を変える `templates`）・条件（`showIf`・`hideIf`・`enabledIf`・`highlightIf`）。
- **焦点と入力。** マウス・ホイール・スクロール条のつまみで押せ、方向キーやゲームパッドでは部品から部品へ焦点を移して押せます（移り先は部品の位置から決まります）。Modal を開くと焦点は中へ移り、閉じると戻ります。ブラウザの `attach` は方向キーと Enter／Space を受けます。UE のキーとゲームパッド、ブラウザのゲームパッドは、あなたのプログラムが受けて描画器の焦点の移動（UE は `MoveFocus`、ブラウザは `R.moveFocus`）を呼びます（§5.2）。
- **文字欄と日本語入力。** `TextInput` は日本語入力（IME）が使えます。中身は `bindVar` の名前で読み書きします。
- **文字の選択とコピー。** 器（`Panel`・`VerticalLayout`・`ScrollView`）に `selectable` を書くと、遊ぶ人がその中の文字をマウスでなぞって選び、クリップボードへ写せます。
- **動き。** `anim` で、初めて出たときの出方（`fade`・`slide`・`pop`）・数の数え上げ・脈動・一覧を順に出す遅れなど 9 つ。
- **テーマ。** 内蔵 3 つ（`atelier`・`midnight`・`sakura`）。テーマの JSON で色や形を替えられます。UE では窓の形・縁・影・濃淡・文字の縁取りや光などの見た目の鍵も描きます（§7）。
- **検査。** 描く前の診断が、知らない部品・綴り違いの鍵・型違い・列挙の外の値・同じ id・名前の色などを、JSON の場所つきで返します（コード 8 種。[リファレンス](momiji-ui-reference.md) §10）。
- **書体を同梱。** BIZ UDP ゴシック・BIZ UDP 明朝（Regular・Bold）・手書き風の Yomogi・ドットの DotGothic16（どれも SIL OFL 1.1）。OS の書体に頼らないので、英語版の Windows でも日本語が出ます。
- **ブラウザ版は依存なし。** ES module 1 本（と `<script>` で読む従来形）で動きます（配布物は GitHub の Releases。§6）。
- **使われている所。** kaedeasset の KaedeRPG（RPG 制作ツール）と KaedeVN（ノベルゲームエンジン）のゲームの画面は、MomijiUI で描いています。

| 部品 17 種（UE） | 文字欄に日本語を打った所（UE） |
|---|---|
| ![部品 17 種を 1 枚に並べた画面](images/momiji-ui/components.png) | ![文字欄に「かえで」を打った所](images/momiji-ui/text-input.png) |

| テーマ atelier | テーマ midnight | テーマ sakura |
|---|---|---|
| ![内蔵テーマ atelier の設定の画面](images/momiji-ui/theme-atelier.png) | ![内蔵テーマ midnight の設定の画面](images/momiji-ui/theme-midnight.png) | ![内蔵テーマ sakura の設定の画面](images/momiji-ui/theme-sakura.png) |

| KaedeRPG のメニュー（テーマ 4 つ） | KaedeVN のコンフィグ |
|---|---|
| ![KaedeRPG のメニューを 4 つのテーマで描いた所](images/momiji-ui/kaederpg-menus.png) | ![KaedeVN のコンフィグの画面](images/momiji-ui/kaedevn-config.png) |

## 3. 要るもの

| もの | 版・条件 |
|---|---|
| Unreal Engine | 5.8 |
| OS | Windows（確かめているのは Windows だけです） |
| Visual Studio | 2022 の最新（17.14）か 2026 の最新。ワークロード「C++ によるゲーム開発」 |
| プロジェクト | C++ のプロジェクト（MomijiUI は C++ から使います。Blueprint の部品はありません） |
| ブラウザ版を使うとき | 静的な HTTP のサーバー（`file://` では書体と `fetch` が働きません）。確かめているのは Windows の Chrome（ほかに Chromium・Firefox・WebKit） |

## 4. あなたのプログラムの仕事（4 つ）

UE でもブラウザでも、あなたのプログラムがすることは同じ 4 つです。

1. **画面 JSON を読む**（一度だけ）。
2. **状態から値を作って描く。** 文字・数・一覧を渡します。
3. **押された動作（`action`）で状態を変える。** `action` の意味はあなたが決めます。
4. **描き直す。** 2 に戻ります。

入力（押す・ホイール・スクロール・スライダーのドラッグ・文字欄の編集・日本語入力）は MomijiUI が受け持ちます。あなたのプログラムに届くのは事象だけで、座標や当たりを自分で解釈する必要はありません。方向キーと Enter は、ブラウザでは `attach` が受け持ち、UE ではあなたのプログラムがキャンバスの `MoveFocus` と `FocusedAction` へ渡します（§5.2）。

| 仕事 | UE | ブラウザ |
|---|---|---|
| 1 読む | `KaedeCore::LoadScreenTree(<JSON の文字列>)` | `MomijiUICanvas.load(json)` |
| 2 描く | `KaedeUI::PresentScreen(Canvas, Tree, Values, Theme, Bg)` | `MomijiUICanvas.present(R, tree, values, bg)` |
| 3 受ける | `Canvas->SetOnAction(…)` | `MomijiUICanvas.attach(R, canvas, { onEvent, render, … })` |
| 4 描き直す | もう一度 `PresentScreen` | もう一度 `present`（`render`） |

ブラウザの `onEvent` には種類つきの事象（押した・文字欄で Enter・スライダー・トグル）と値が届きます。UE の `SetOnAction` に届くのは `action` の文字列とスライダーの位置だけで、文字欄の中身は `GetTextInputValue` で読みます。事象の形は[リファレンス](momiji-ui-reference.md) §9 にあります。

## 5. UE で使う

### 5.1 準備

あなたのモジュールの `*.Build.cs` で、`MomijiUICore` と `MomijiUISlate` に依存させます。

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "MomijiUICore", "MomijiUISlate" });
```

書体は `MomijiUISlate` が書き出しのときに一緒に積みます（あなたがすることはありません）。

### 5.2 ウィジェットを書く

見本 `Source/MomijiUISlate/Private/Examples/SMomijiUICounterExample.cpp`（プラグインのフォルダの中）が、4 つの仕事をそのまま書いた最小のウィジェットです。要所は次のとおりです。

```cpp
#include "SKaedeUiCanvas.h"
#include "MomijiUICore.h"

// 1. 画面 JSON を読む（一度だけ）
FString Json;
FFileHelper::LoadFileToString(Json, *Path);
Tree = KaedeCore::LoadScreenTree(TCHAR_TO_UTF8(*Json));
Theme = KaedeCore::BuiltinTheme("midnight");

// キャンバスを置き、押された action を受ける
ChildSlot[ SAssignNew(Canvas, SKaedeUiCanvas) ];
Canvas->SetOnAction([this](const FString& Action, float /*RelX*/) { HandleAction(Action); });

// 2. 状態から値を作って描く（3 で状態を変えたら、4 でもう一度呼ぶ）
KaedeUI::FKaedeScreenValues V;
V.Strings["title"] = "…";
V.Vars["count"] = Count;
KaedeUI::PresentScreen(*Canvas, Tree, V, Theme, FLinearColor::FromSRGBColor(FColor(0x0e, 0x11, 0x16)));
```

- ゲームの窓に出すには `GEngine->GameViewport->AddViewportWidgetContent(SNew(あなたのウィジェット))` です。
- 自分の UI をマウスで押させるときは、プレイヤーコントローラーでマウスの矢印を出し、入力を UI にも渡します（`bShowMouseCursor = true` と `SetInputMode(FInputModeGameAndUI())`）。空のプロジェクトの既定のゲームは、マウスを取って矢印を隠すためです。
- マウスの押す・ホイール・スクロール条のつまみ・スライダーのドラッグはキャンバスが受けます。**キーボードとゲームパッドはキャンバスが自分では受けません。** あなたのプログラムが入力（Enhanced Input など）で受け、方向なら `Canvas->MoveFocus(dx, dy)`（dx・dy は -1・0・+1）と `Canvas->SetFocusVisible(true)`、決定なら `Canvas->FocusedAction()` で焦点の部品の `action` を取って、押されたときと同じ処理をします。
- 文字欄は `SKaedeUiCanvas` が自分で編集欄を重ねます（日本語入力も）。中身は `Canvas->GetTextInputValue("<bindVar>")` で読み、`SetTextInputValue` で入れます。
- `PresentScreen` の最後の引数に画面の名前を渡すと、文字欄の中身・スクロール量・焦点を画面ごとに分けて持ちます。捨てるのは `ResetScreenState(<画面の名前>)` です。
- 動き（`anim`）を使う画面は、`kaedevn::ui::UiMotionState` をウィジェットで持ち、`PresentScreen` に渡して、Tick で `update(<ミリ秒>)` を呼びます。
- テーマは `KaedeCore::BuiltinTheme("<名前>")` か `KaedeCore::LoadTheme(<テーマ JSON の文字列>)`、書体は `Canvas->SetFontFamily(…)` で替えます。

呼ぶものの一覧は[リファレンス](momiji-ui-reference.md) §9.2 にあります。

### 5.3 画面だけを見る

値を組むコードを書く前に、画面 JSON の見た目だけを確かめたいときは、1.6 の起動引数 `-KaedeUiShot=<画面 JSON>` を使います。1 枚撮って終わり、`Saved\Screenshots\WindowsEditor\MomijiUI.png` に書きます。JSON に `"bind": { "strings": {…}, "vars": {…}, "lists": {…} }` を書いておけば、その値で描きます（`bind` は描くときには読まれず、この撮影と照合のためだけの鍵です）。

### 5.4 検査をいつ呼ぶか

`KaedeCore::ValidateScreenJson` は検査の道具で、描くときには呼びません。1.7 のコマンドのように開発中に呼ぶか、自動テストの中で、あなたの画面 JSON を全部通して `error` が 0 件かを見るのが向いています。読み込み（`LoadScreenTree`）は、型違いの鍵をその鍵だけ既定値にして読み続けます（止まりません）。

## 6. ブラウザで使う

ブラウザ版の配布物（自分の HTML から読むファイル）は、GitHub の Releases（<https://github.com/kaedeasset/momiji-ui/releases>）の `momiji-ui-web-<版>.zip` です。開いた中身を自分のサイトに置きます。源から作るなら、リポジトリ（<https://github.com/kaedeasset/momiji-ui>）で `node Tools/momiji-ui/web/build.mjs` を回すと `Tools/momiji-ui/web/dist/` にできます。動きは説明書の Web のトップ（<https://momiji.kaedeasset.com/>）の見本でも見られます。配布物の中身は次のとおりです。

| ファイル | 何 |
|---|---|
| `momiji-ui.mjs` | 本体（ES module・依存なし） |
| `momiji-ui-canvas.js` | `<script src>` で読む従来形（`window.MomijiUICanvas`）。同じ源から機械で作ったもの |
| `momiji-ui.css` | 標準のスタイル（1280×720 の板・canvas・文字欄の `<input>`・同梱書体の読み込み） |
| `fonts/` | BIZ UDP ゴシック（Regular・Bold）と許諾文 |
| `examples/counter/` | 1.5 の見本（画面 JSON は UE の見本と同じファイル） |

使い方の形（UE と同じ 4 つの仕事）:

```js
import { MomijiUICanvas } from './momiji-ui.mjs';
const canvas = document.getElementById('screen');
const R = new MomijiUICanvas.Renderer(canvas);
R.setTheme('midnight');
const { root: tree } = MomijiUICanvas.load(await (await fetch('counter.json')).json());   // 1. 読む（error があれば止まる）
await MomijiUICanvas.loadFonts();
const render = () => MomijiUICanvas.present(R, tree, values(), '#0e1116');                // 2. 描く
const host = MomijiUICanvas.attach(R, canvas, {
  stage: document.getElementById('stage'), render, fit: true,
  onEvent(ev) { /* 3. ev.action で状態を変える */ render(); },                              // 4. 描き直す
});
```

- `<canvas>` は 1280×720 の板（`class="momiji-ui-stage"`）の中に置き、静的な HTTP のサーバーで開きます。
- canvas では日本語入力ができないので、`attach` が文字欄の位置に `<input>` を重ねます。中身は `host.inputValue('<bindVar>')` で読み、`host.setInputValue` で入れます。
- 終わるときは `host.dispose()` です。
- テーマは色の 11 の鍵だけを描きます（形の鍵は UE だけ。§7）。書体はゴシックだけです。

呼ぶものの一覧は[リファレンス](momiji-ui-reference.md) §9.3 にあります。

## 7. UE とブラウザで違う所

| こと | UE | ブラウザ |
|---|---|---|
| 配置・押せる場所・焦点の動き・スクロール・折り返しの行数・検査の結果 | 同じ | 同じ |
| 字や角の塗り方（なめらかさ・濃さ） | Slate の描き方 | canvas の描き方（字が少し濃く見えます） |
| 見た目の鍵（窓の形・縁・影・濃淡・文字の縁取りと光・字間など） | 描く | 検査では知っている鍵として通すが、描かない |
| テーマの形と飾りの鍵（18） | 描く | 色の 11 の鍵だけ描く |
| 部品 `Polygon`・`Divider` | 描く | 板の色の四角で描く |
| 動きの `blink`・`bob`・`frames`・`exit`・`tween` | 動く | 動かない |
| `anim.enter.type` の `"open"` | 描く（ただし検査は `bad-value` の error） | 検査で止まる |
| 書体 | ゴシック・明朝・手書き風・ドット | ゴシックだけ |

どの鍵が「UE だけ」かは、[リファレンス](momiji-ui-reference.md)の表の「ブラウザ」の列にあります。UE とブラウザで同じ見た目にしたい画面は、「UE だけ」の鍵を使わずに書きます。

## 8. まだできないこと

- **Blueprint と UMG からは使えません。** C++ から使います。
- **ブラウザ版は npm では配っていません。** GitHub の Releases の zip から取ります（§6）。
- **見た目の鍵とテーマの形は、ブラウザではまだ描きません**（§7）。
- **読み上げ（スクリーンリーダー）には対応していません。**
- **UE は Windows だけで確かめています。** Mac・Linux の UE と、iOS・Android は確かめていません。ブラウザも、製品版の Safari（macOS）・Edge・Mac と Linux のブラウザは確かめていません。
- **エディタの中で画面 JSON を組む道具はありません。** 画面 JSON はテキストエディタで書き、`-KaedeUiShot`（§5.3）と検査（1.7）で確かめます。

## 9. 困ったとき

| 起きたこと | 原因と直し方 |
|---|---|
| 1.4 で ▶ を押しても見本が出ない | エディタを `-KaedeUiExample=counter` をつけて起動していない。出るのはその起動の最初のプレイだけなので、一度出したらエディタを閉じて起動し直す。それでも出なければ、出力ログの `[ui-example]` の行（見本の画面が読めないなど）を見る |
| 見本や自分の UI のボタンを押しても反応しない・マウスの矢印が消える | ゲームがマウスを取っている。プレイ中は **Shift+F1** で戻す。自分のゲームでは §5.2 のとおりプレイヤーコントローラーでマウスの矢印を出す |
| 1.6 で絵が撮れない・窓がすぐ閉じる | `-KaedeUiShot` の場所が違うか、JSON として読めない。出力ログ（`Saved\Logs\MyUi.log`）の `[ui-shot]` の行を見る |
| 1.7 の `Momiji.Check` が「コマンドが無い」 | 組み立てていないか、`MomijiCheck.cpp` を足したモジュールが違う。エディタを閉じて Visual Studio でもう一度組み立てる |
| 組み立てで `MomijiUICore.h` が見つからない | `*.Build.cs` に `"MomijiUICore"` を足していない（1.7 の手順 2） |
| 子が出ない・大きさ 0 で消える | `Panel` は子を並べない（子の `x` `y` `w` `h` がそのまま使われる）。並べるなら `VerticalLayout` |
| `{name}` がそのまま出る | 値を渡していない（知らない鍵はそのまま残る）。鍵の綴りを確かめる |
| 条件が効かない | 条件式の名前は数（`vars`）で渡す。文字（`strings`）は条件に使えない |
| 色が変わらない | 色は `#rrggbb` か `#rrggbbaa` だけ（`red` などは無視される。検査の `bad-color`） |
| 字が思ったより大きい | `fontSize` はポイント（画面では ×4/3 px） |
| 動かない（`anim`） | 動く部品に `id` か `action` が無い |
| 方向キーで選んだ場所が描き直しで飛ぶ | 同じ `action` のボタンが複数あって `id` が無い。状態を持つ部品には `id` を付ける（一覧の中は `"id": "row:{item.id}"`） |
| ブラウザで折り返しがずれる | 書体を読み終える前に描いている。`await MomijiUICanvas.loadFonts()` を待ってから描く |
| ブラウザで日本語が打てない | 文字欄に `<input>` を重ねていない。`attach` を使う |
