# 同梱フォント

| ファイル | 書体 | 出所 |
|---|---|---|
| `BIZUDPGothic-Regular.ttf` / `-Bold.ttf` | BIZ UDP ゴシック（モリサワ） | https://github.com/googlefonts/morisawa-biz-ud-gothic （`fonts/ttf/`） |
| `BIZUDPMincho-Regular.ttf` / `-Bold.ttf` | BIZ UDP 明朝（モリサワ） | https://github.com/googlefonts/morisawa-biz-ud-mincho （`fonts/ttf/`） |
| `OFL-BIZ-UDGothic.txt` / `OFL-BIZ-UDMincho.txt` | ライセンス文（SIL Open Font License 1.1） | 各リポジトリの `OFL.txt` |
| `Yomogi-Regular.ttf` / `OFL-Yomogi.txt` | Yomogi（Satsuyako・手書き風。RPG のテーマ sketch） | https://github.com/google/fonts （`ofl/yomogi/`・2026-10-12 取得） |
| `DotGothic16-Regular.ttf` / `OFL-DotGothic16.txt` | DotGothic16（Fontworks・ドット。RPG のテーマ dust） | https://github.com/google/fonts （`ofl/dotgothic16/`・2026-10-12 取得） |

2026-10-02 に取得（無改変）。読む側は `Source/MomijiUISlate/Private/KaedeUiFonts.cpp` の `FindJpFontFile`、
配信物へ積むのは `Source/MomijiUISlate/MomijiUISlate.Build.cs` の `RuntimeDependencies`（loose file）。
以前の OS フォント（游ゴシック／游明朝）は読まない。

- OFL 1.1 なので同梱・商用配布は可。ライセンス文を一緒に配る（Build.cs が積む）。改変して配るときは予約書体名を使わない。
- 差し替えるときはこの表とファイル名、通知（梱包の `THIRD_PARTY_NOTICES.txt` の元の表）、`FindJpFontFile` を同じ日に直す。
