// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
#pragma once

// Tween — UIモーションの実行時状態（UiMotionState）。
//
// UIツリーは毎フレーム prune/expandRepeats で作り直されるため、アニメ状態は
// ツリーに持たせられない。ここに外置きし「安定キー」で引く（設計 07/21/04 §A1）:
//   key = <scope> "/" <name>
//     scope = 画面パス: screenId（"craft" 等） / HUDパス: 固定 "hud"
//     name  = ノードの id（あれば優先）/ 無ければ action（repeat 補間後＝データの同一性）
// 「どこにあるか」（配列番号・座標・構造パス）には決して紐づけない。位置は再構築のたびに
// 別のノードを指し得るため、それが 07/21 の「押した本人と違うボタンが動く」の根源だった。
//
// 使い方（ホスト＝埋め込み先ウィジェット）:
//   Rebuild 時: beginSync(screenId) → 走査中に noteNode(...) → endSync()
//   毎 Tick  : update(deltaMs)（アクティブが無ければ実質ゼロコスト）
//   描画時   : sample(key, out) で alpha/offset/scale/表示テキストを合成
//
// 決定論: 全トラックは「経過msを与えると値が決まる」純粋な作り。テストは
// update(deltaMs) を手動ステップして値をアサートする（widget 不要）。
// UE 非依存（標準ライブラリのみ）。

#include "../core/UiAnim.hpp"
#include "Easing.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace kaedevn::ui {

// sample() の出力。既定値のままなら「モーション無し＝素通し」と同じ。
struct UiMotionSample {
    float alpha = 1.f;             // 乗算アルファ（enter fade/slide/pop）
    float offX = 0.f, offY = 0.f;  // 論理px オフセット（enter slide）
    float scale = 1.f;             // ノード矩形中心基準のスケール（pop/pulse）
    bool  textFixed = false;       // true なら scale を文字に掛けない＝箱だけが脈動する（pulse 単独のとき。2026-10-06 ユーザー判断「文字が変化すると違和感」）
    bool  hasText = false;         // true なら表示テキストを上書き（countUp）
    std::string text;
    // ---- 2026-10-12 ----
    float scaleY = 1.f;            // 縦だけの拡大（enter open・exit close。ピボット＝ノードの中心）
    int   frame = -1;              // コマ送りの今のコマ（-1 は無し）
    float valueScale = 1.f;        // tween: 描く棒の長さ ÷ 目標の長さ
};

class MOMIJIUICORE_API UiMotionState {
public:
    // ---- Rebuild 時の同期（ツリー走査と1:1）----
    // screenId が変わったら画面遷移エンター（screenEnterSample）を開始する。キーは破棄しない
    //   （旧画面のキーは endSync の GC で自然消滅し、scope 固定の HUD キーは生き残る）。
    void beginSync(const std::string& screenId);
    // 走査中のモーション付きノードを登録する。
    //   groupKey: stagger の順位を数えるグループ（repeat 親＝リストノードの安定キー）。
    //             遅延順位は「同じ同期で新規（enter発火）になった同じグループ内での順位」で、
    //             表示位置ではない（画面内で1行だけ増えたら遅延0で即スライドイン）。
    //   hasNumeric/numeric: countUp 対象の現在値（テキストが数値のときだけ true）
    //   numericGrouped: 元テキストが3桁区切り(,)表示だったか（補間表示も同じ整形にする）
    //   pulseActive: pulse whileIf の評価結果（ホストが evalCond 済み）
    void noteNode(const std::string& key, const std::string& groupKey, const UiAnimSpec& spec,
                  bool hasNumeric, double numeric, bool numericGrouped, bool pulseActive);
    // 今回の走査で見えなかったキーの状態を破棄（リーク防止・再出現で enter 再発火）。
    void endSync();

    // 安定キーの検証エラー（painter が検出。設計 07/21/04 §A2）。フォールバックは作らず
    // 必ずログに出す＝開発時に必ず気づく。drainLogs が "ERROR ..." として排出する。
    void reportError(const std::string& message);

    // ---- 毎フレーム ----
    void update(double deltaMs);

    // ---- 描画時 ----
    // key にアクティブなモーションが無ければ false（呼び出し側は素通し）。
    bool sample(const std::string& key, UiMotionSample& out) const;

    // 画面遷移エンター（基本トランジション。07/21 操作感）: beginSync で screenId が
    // 変わったとき自動開始する 160ms の fade+上昇。描画側が「画面コマンド範囲」
    //（HUD/トースト以外）へ一括適用する。アニメ中のみ true。
    bool screenEnterSample(UiMotionSample& out) const;

    // アクティブなトラック数（enter/countUp の進行中 + pulse 作動中）。静止時 0 の検証用。
    int activeCount() const;

    // 消えていく途中のキー（exit）。描画側は前に描いたそのキーの描画命令を、sample を掛けて描き続ける
    std::vector<std::string> exitingKeys() const;
    // Slider の値（tween）。noteNode と同じ同期の中で呼ぶ
    void noteValue(const std::string& key, double value);

    // 検証ログ（"enter begin key=... " 等）。ホストが drain して UE_LOG へ流す。
    std::vector<std::string> drainLogs();

private:
    struct Entry {
        UiAnimSpec spec;          // 値コピー（ツリーは Rebuild で消えるため参照不可）
        bool seenThisSync = false;

        // enter
        bool  enterActive = false;
        double enterElapsedMs = 0.0;   // delay を含めた経過
        int   enterDelayMs = 0;        // spec.delay + stagger
        // countUp
        bool  hasValue = false;
        double lastValue = 0.0;
        bool  countActive = false;
        double countElapsedMs = 0.0;
        double countFrom = 0.0, countTo = 0.0;
        bool  countGrouped = false;    // 桁区切り(,)で表示するか（元テキスト準拠）
        // pulse
        bool  pulseActive = false;
        double pulseElapsedMs = 0.0;
        // 繰り返し（blink・bob・frames）
        double loopElapsedMs = 0.0;
        // exit
        bool  exitActive = false;
        double exitElapsedMs = 0.0;
        // tween
        bool  hasTweenValue = false;
        bool  tweenActive = false;
        double tweenElapsedMs = 0.0, tweenFrom = 0.0, tweenTo = 0.0;
    };

    std::string ScreenId;
    bool bSyncing = false;
    std::unordered_map<std::string, Entry> Entries;
    // 同期中だけ持つ stagger カウンタ（groupKey → その同期で新規 enter した数）。endSync で破棄。
    std::unordered_map<std::string, int> GroupEnterCount;
    std::vector<std::string> Logs;
    // 画面遷移エンター
    bool   ScreenEnterActive = false;
    double ScreenEnterElapsedMs = 0.0;
};

// countUp の表示整形: 整数は formatNum 相当（小数は最短表記）、grouped なら3桁区切り。
MOMIJIUICORE_API std::string formatCountValue(double v, bool grouped);

}  // namespace kaedevn::ui
