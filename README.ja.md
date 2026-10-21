# MomijiUI

画面を JSON で書く UI の仕組みです。同じ画面 JSON を、Unreal Engine 5（Slate）とブラウザ（canvas）の両方で同じように描きます。MIT ライセンスで、無料で使えます。

英語版は [README.md](README.md)。**この日本語版が正**で、英語版はその訳です。

- リポジトリ: <https://github.com/kaedeasset/momiji-ui>
- 作者: kaedeasset（<https://kaedeasset.com/>）
- 対応: Unreal Engine 5.8・Windows（MSVC 14.51）。ブラウザ版は Node 18 以上で組み立て、静的な HTTP 配信で動きます（依存なし）

## 中身

| 置き場 | 何か |
|---|---|
| `Plugins/MomijiUI/` | UE のプラグイン。モジュールは `MomijiUICore`（画面 JSON の読み込み・配置・検査。エンジン非依存）と `MomijiUISlate`（Slate の描画）の 2 つ。書体・照合の見本（`Fixtures/`）・最小の見本（`Plugins/MomijiUI/Examples/counter.json`）・自動試験のソースも入っています |
| `Tools/momiji-ui/` | ブラウザ版。源 `web/src/momiji-ui.mjs`・配布物を作る `web/build.mjs`・画面 JSON の検査 `validate.mjs`・見本 `examples/`・照合の道具（`package.json` と lock） |
| `Scripts/lib/momiji-ui-canvas.js` | `<script>` で読む形のブラウザ版（`web/build.mjs` が源から作る生成物） |
| `Plugins/MomijiUI/Docs/` | 説明書。導入ガイド `momiji-ui-guide.md`（見本を動かす・画面 JSON を書く・UE とブラウザで使う・違い・つまずき）とリファレンス `momiji-ui-reference.md`（部品・鍵・列挙・動き・テーマ・診断の全部）の日英と、更新履歴 `momiji-ui-changelog.md`。Fab の zip の `Docs/` と同じもの |

## 入れ方

**UE:** `Plugins/MomijiUI/` を自分のプロジェクトの `Plugins/` に写し、*Edit → Plugins* で MomijiUI を有効にして、プロジェクトを組み立てます。

**ブラウザ:** 配布物を作り、そのフォルダを静的な HTTP で配ります。
```
node Tools/momiji-ui/web/build.mjs        # → Tools/momiji-ui/web/dist/
node Tools/momiji-ui/examples/serve.mjs   # → http://localhost:8840/counter/ を開く
```

## 最小の使い方

いちばん小さい見本は、押すと数が増えるカウンター `Plugins/MomijiUI/Examples/counter.json` です。仕事は 4 つ: 画面 JSON を読む → 状態から値を作って描く → 押された action で状態を変える → 描き直す。

- ブラウザ: `Tools/momiji-ui/examples/counter/main.js`
- UE: `Plugins/MomijiUI/Source/MomijiUISlate/Private/Examples/SMomijiUICounterExample.cpp`（ゲームの窓に出すには `GEngine->GameViewport->AddViewportWidgetContent(SNew(SMomijiUICounterExample))`）

画面 JSON が規則どおりかは、次で確かめます。
```
node Tools/momiji-ui/validate.mjs Plugins/MomijiUI/Examples/counter.json
```

詳しくは導入ガイド [Plugins/MomijiUI/Docs/momiji-ui-guide.md](Plugins/MomijiUI/Docs/momiji-ui-guide.md) と、全部の部品と鍵を引けるリファレンス [Plugins/MomijiUI/Docs/momiji-ui-reference.md](Plugins/MomijiUI/Docs/momiji-ui-reference.md)（ガイドの §1 は Fab で入れた人の手順です）。

## 試験

- UE の自動試験: 試験のソースは `Plugins/MomijiUI/Source/*/Private/*Tests.cpp`。プロジェクトをエディタで開き、*Tools → Session Frontend → Automation* で `MomijiUI` を選んで回します。コマンドなら `UnrealEditor-Cmd.exe <あなたの>.uproject -ExecCmds="Automation RunTests MomijiUI; Quit" -unattended -nullrhi`
- UE とブラウザの照合: 開発元のリポジトリでは、`Fixtures/` の画面を UE とブラウザで描いて比べる脚本（`momiji-ui-conformance.ps1`・`momiji-ui-examples.ps1`）を回しています。ブラウザを複数で比べるときは `cd Tools/momiji-ui; npm install; npx playwright install chromium firefox webkit` で道具を入れます

## Fab と、有料の商品

- Fab の商品ページ: 準備中
- MomijiUI と KaedeVN（ノベルゲームエンジン <https://github.com/kaedeasset/kaedevn>）は無料です。kaedeasset は、これを土台にした有料の商品を作っています。いまの柱は **KaedeRPG**（RPG 制作ツール。Unreal Engine のプラグイン）です。案内は <https://kaedeasset.com/> にあります。

## ライセンス

MomijiUI のソースは MIT です（[LICENSE](LICENSE)。© 2026 kaedeasset）。

MIT でないもの・別のライセンスのもの:

| もの | ライセンス | 置き場 |
|---|---|---|
| 書体 BIZ UDPゴシック・BIZ UDP明朝（Morisawa） | SIL Open Font License 1.1 | `Plugins/MomijiUI/Resources/Fonts/`（`OFL-BIZ-UDGothic.txt`・`OFL-BIZ-UDMincho.txt`） |
| 書体 Yomogi・DotGothic16 | SIL Open Font License 1.1 | 同じ場所（`OFL-Yomogi.txt`・`OFL-DotGothic16.txt`） |
| nlohmann/json 3.11.3 | MIT（© Niels Lohmann） | `Plugins/MomijiUI/Source/MomijiUICore/ThirdParty/nlohmann/json.hpp` |

書体は手を加えずに同梱しています。

"Unreal" と "Unreal Engine" は Epic Games, Inc. の商標です。MomijiUI は Epic Games とは関係のない、非公式のプラグインです。
