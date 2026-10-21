// Copyright (c) 2026 kaedeasset. Licensed under the MIT License.
// Tween.cpp — UIモーションの実行時状態（UiMotionState）の実装。
//
// UIツリーは毎フレーム作り直されるのでアニメ状態をツリーに持たせられない。
// ここで外置きし「安定キー（画面 + ノードid or action）」で引く。
// **キーを配列番号や座標に紐づけてはいけない。** 位置は再構築のたび別ノードを指し得る。
// それが 2026-07 の「押した本人と違うボタンが動く」不具合の原因だった。
//
// 全トラックは「経過msを与えると値が決まる」純粋な作りで、テストは update(deltaMs) を
// 手動ステップして値を assert する（widget 不要）。
//
// 概要記述: Claude Opus 5 (1M context) — 2026-07-28
#include "Tween.hpp"

#include "Interp.hpp"   // formatNum（countUp の数値整形を {var} 補間と一致させる）

#include <cmath>

namespace kaedevn::ui {

namespace {

// enter 種別の既定イージング（設計 07/21/01: pop/slide=easeOutCubic、fade=easeOutQuad）。
Easing enterEasingOf(const UiAnimSpec& spec)
{
    if (!spec.enterEasing.empty()) return easingFromString(spec.enterEasing);
    return spec.enterType == "fade" ? Easing::EaseOutQuad : Easing::EaseOutCubic;
}

}  // namespace

std::string formatCountValue(double v, bool grouped)
{
    std::string s = formatNum(v);
    if (!grouped) return s;
    // 整数部にだけ3桁区切りを入れる（負号・小数部は保持）。
    size_t begin = (!s.empty() && s[0] == '-') ? 1 : 0;
    size_t end = s.find('.');
    if (end == std::string::npos) end = s.size();
    if (end - begin > 3) {
        for (size_t i = end - 3; i > begin; i -= 3) {
            s.insert(i, ",");
            if (i <= 3) break;   // size_t アンダーフロー防止
        }
    }
    return s;
}

void UiMotionState::beginSync(const std::string& screenId)
{
    if (screenId != ScreenId) {
        ScreenId = screenId;
        // 基本トランジション（全画面共通の fade+上昇）を開始。初回表示も含む。
        // キーは破棄しない（設計 07/21/04 §B1）: 旧画面のキーは endSync の GC で自然に消え、
        // scope 固定の HUD キー（countUp 前回値・pulse 位相）はタブ切替を跨いで生き残る。
        ScreenEnterActive = true;
        ScreenEnterElapsedMs = 0.0;
    }
    for (auto& [K, E] : Entries) E.seenThisSync = false;
    GroupEnterCount.clear();
    bSyncing = true;
}

void UiMotionState::noteNode(const std::string& key, const std::string& groupKey,
                             const UiAnimSpec& spec,
                             bool hasNumeric, double numeric, bool numericGrouped, bool pulseActive)
{
    if (!bSyncing) return;
    auto It = Entries.find(key);
    const bool bNew = (It == Entries.end());
    if (!bNew && It->second.seenThisSync) {
        // 同一同期内のキー重複＝データ検証エラー（設計 07/21/04 §A2）。先に登録された方の
        // 状態を正とし、2件目は無視する（フォールバックを作らず、ログで必ず気づかせる）。
        reportError("key-dup key=" + key);
        return;
    }
    Entry& E = bNew ? Entries[key] : It->second;
    E.seenThisSync = true;
    E.spec = spec;
    if (E.exitActive) { E.exitActive = false; E.exitElapsedMs = 0.0; }

    if (bNew) {
        if (spec.hasEnter()) {
            // stagger の順位は「この同期で新規になった、同じグループの中での順位」。
            // 表示位置ではないので、画面内で1行だけ増えても遅延0で即スライドインする。
            const int Rank = GroupEnterCount[groupKey]++;
            E.enterActive = true;
            E.enterElapsedMs = 0.0;
            E.enterDelayMs = spec.enterDelayMs + spec.staggerMs * Rank;
            Logs.push_back("enter begin key=" + key + " type=" + spec.enterType +
                           " delayMs=" + formatNum((double)E.enterDelayMs));
        }
        if (hasNumeric) {
            E.hasValue = true; E.lastValue = numeric;   // 初回は即時表示
            // 「キーが作り直され続けている」＝毎回これが出る、を実プレイで見分けるためのログ。
            if (spec.hasCountUp)
                Logs.push_back("countUp bind key=" + key + " value=" + formatNum(numeric) +
                               " (first=immediate)");
        }
    }
    else if (hasNumeric && spec.hasCountUp) {
        if (E.hasValue && E.lastValue != numeric) {
            E.countActive = true;
            E.countElapsedMs = 0.0;
            E.countFrom = E.lastValue;
            E.countTo = numeric;
            Logs.push_back("countUp begin key=" + key + " from=" + formatNum(E.countFrom) +
                           " to=" + formatNum(E.countTo) + " durationMs=" +
                           formatNum((double)spec.countUpDurationMs));
        }
        E.hasValue = true;
        E.lastValue = numeric;
    }
    if (hasNumeric) E.countGrouped = numericGrouped;

    E.pulseActive = spec.hasPulse && pulseActive;
    if (!E.pulseActive) E.pulseElapsedMs = 0.0;   // 位相リセット（次の作動は 1.0 から）
}

void UiMotionState::reportError(const std::string& message)
{
    Logs.push_back("ERROR " + message);
}

void UiMotionState::endSync()
{
    bSyncing = false;
    GroupEnterCount.clear();
    for (auto It = Entries.begin(); It != Entries.end(); ) {
        Entry& E = It->second;
        if (!E.seenThisSync && E.spec.hasExit() && !E.exitActive && E.exitElapsedMs == 0.0) {
            // 消えた。exit があれば、しばらく描き続けて消す（描画側が前の描画命令を持っている）
            E.exitActive = true;
            E.exitElapsedMs = 0.0;
            Logs.push_back("exit begin key=" + It->first);
            ++It;
        }
        else if (!E.seenThisSync && !E.exitActive) It = Entries.erase(It);
        else ++It;
    }
}

// 画面遷移エンターの尺（全画面共通の基本トランジション）。
static constexpr double kScreenEnterMs = 160.0;

void UiMotionState::update(double deltaMs)
{
    if (deltaMs <= 0.0) return;
    if (ScreenEnterActive) {
        ScreenEnterElapsedMs += deltaMs;
        if (ScreenEnterElapsedMs >= kScreenEnterMs) ScreenEnterActive = false;
    }
    for (auto& [K, E] : Entries) {
        if (E.enterActive) {
            E.enterElapsedMs += deltaMs;
            if (E.enterElapsedMs >= E.enterDelayMs + E.spec.enterDurationMs) {
                E.enterActive = false;
                Logs.push_back("enter end key=" + K +
                               " elapsed=" + formatNum(E.enterElapsedMs));
            }
        }
        if (E.countActive) {
            E.countElapsedMs += deltaMs;
            if (E.countElapsedMs >= E.spec.countUpDurationMs) {
                E.countActive = false;
                Logs.push_back("countUp end key=" + K + " value=" + formatNum(E.countTo));
            }
        }
        if (E.pulseActive) E.pulseElapsedMs += deltaMs;
        if (E.spec.hasLoop()) E.loopElapsedMs += deltaMs;
        if (E.tweenActive) {
            E.tweenElapsedMs += deltaMs;
            if (E.tweenElapsedMs >= E.spec.tweenDurationMs) E.tweenActive = false;
        }
        if (E.exitActive) E.exitElapsedMs += deltaMs;
    }
    // 消え終わった exit を捨てる
    for (auto It = Entries.begin(); It != Entries.end(); ) {
        if (It->second.exitActive && It->second.exitElapsedMs >= It->second.spec.exitDurationMs) It = Entries.erase(It);
        else ++It;
    }
}

void UiMotionState::noteValue(const std::string& key, double value)
{
    auto It = Entries.find(key);
    if (It == Entries.end() || !It->second.spec.hasTween) return;
    Entry& E = It->second;
    if (E.hasTweenValue && E.tweenTo != value) {
        // 今の見た目の値から新しい値へ
        const double P = E.tweenActive ? applyEasingSafe(E.tweenElapsedMs / (double)E.spec.tweenDurationMs, Easing::EaseOutCubic) : 1.0;
        E.tweenFrom = E.tweenFrom + (E.tweenTo - E.tweenFrom) * P;
        E.tweenTo = value;
        E.tweenActive = true;
        E.tweenElapsedMs = 0.0;
    }
    else if (!E.hasTweenValue) { E.tweenFrom = E.tweenTo = value; }
    E.hasTweenValue = true;
}

std::vector<std::string> UiMotionState::exitingKeys() const
{
    std::vector<std::string> Out;
    for (const auto& [K, E] : Entries) if (E.exitActive) Out.push_back(K);
    return Out;
}

bool UiMotionState::sample(const std::string& key, UiMotionSample& out) const
{
    const auto It = Entries.find(key);
    if (It == Entries.end()) return false;
    const Entry& E = It->second;
    if (!E.enterActive && !E.countActive && !E.pulseActive && !E.spec.hasLoop() && !E.exitActive && !E.tweenActive) return false;

    out = UiMotionSample{};

    if (E.enterActive) {
        // delay 中は開始値（alpha0 等）で固定。t=0..1 → eased 進行度 p。
        const double Raw = (E.enterElapsedMs - E.enterDelayMs) / (double)E.spec.enterDurationMs;
        const double P = applyEasingSafe(Raw, enterEasingOf(E.spec));
        if (E.spec.enterType == "fade") {
            out.alpha = (float)P;
        } else if (E.spec.enterType == "slide") {
            out.alpha = (float)P;
            const float D = E.spec.enterDistance * (float)(1.0 - P);
            if (E.spec.enterFrom == "left")       out.offX = -D;
            else if (E.spec.enterFrom == "right") out.offX = D;
            else if (E.spec.enterFrom == "top")   out.offY = -D;
            else                                  out.offY = D;   // bottom（既定）
        } else if (E.spec.enterType == "pop") {
            out.alpha = (float)P;
            out.scale = 0.85f + 0.15f * (float)P;
        } else if (E.spec.enterType == "open") {
            out.scaleY = (float)P;
        }
    }
    if (E.spec.hasLoop()) {
        const double T = E.loopElapsedMs;
        if (E.spec.hasBlink && E.spec.blinkPeriodMs > 0) {
            const double W = 0.5 * (1.0 + std::cos(T / E.spec.blinkPeriodMs * 2.0 * 3.14159265358979323846));
            out.alpha *= (float)(E.spec.blinkMin + (1.0 - E.spec.blinkMin) * W);
        }
        if (E.spec.hasBob && E.spec.bobPeriodMs > 0)
            out.offY += E.spec.bobDistance * (float)std::sin(T / E.spec.bobPeriodMs * 2.0 * 3.14159265358979323846);
        if (E.spec.hasFrames)
            out.frame = (int)(T / E.spec.framesPeriodMs) % E.spec.framesCount;
    }
    if (E.exitActive) {
        const double P = 1.0 - applyEasingSafe(E.exitElapsedMs / (double)E.spec.exitDurationMs, Easing::EaseOutQuad);
        if (E.spec.exitType == "close") out.scaleY *= (float)P;
        else out.alpha *= (float)P;
    }
    if (E.tweenActive && E.tweenTo != 0.0) {
        const double P = applyEasingSafe(E.tweenElapsedMs / (double)E.spec.tweenDurationMs, Easing::EaseOutCubic);
        out.valueScale = (float)((E.tweenFrom + (E.tweenTo - E.tweenFrom) * P) / E.tweenTo);
    }

    if (E.countActive) {
        const double Raw = E.countElapsedMs / (double)E.spec.countUpDurationMs;
        const double P = applyEasingSafe(Raw, easingFromString(E.spec.countUpEasing));
        const double V = E.countFrom + (E.countTo - E.countFrom) * P;
        out.hasText = true;
        // 整数同士のカウントは中間値も整数で出す（12.7 のような揺れを見せない）。
        const bool bInt = std::floor(E.countFrom) == E.countFrom &&
                          std::floor(E.countTo) == E.countTo;
        out.text = formatCountValue(bInt ? std::round(V) : V, E.countGrouped);
    }

    if (E.pulseActive && E.spec.pulsePeriodMs > 0) {
        // 1 → scale → 1 の滑らかなループ（cos）。作動開始は必ず 1.0 から。
        const double Phase = E.pulseElapsedMs / (double)E.spec.pulsePeriodMs;
        const double Wave = 0.5 * (1.0 - std::cos(Phase * 2.0 * 3.14159265358979323846));
        out.scale *= 1.f + (E.spec.pulseScale - 1.f) * (float)Wave;
        // 脈動は箱だけ。文字は大きさも位置も変えない（文字が動くと読んでいる目が揺れる）。
        // enter の pop と重なっている間だけは、pop が全体を拡大縮小するので文字も一緒に動く。
        out.textFixed = !(E.enterActive && E.spec.enterType == "pop");
    }

    return true;
}

bool UiMotionState::screenEnterSample(UiMotionSample& out) const
{
    if (!ScreenEnterActive) return false;
    const double P = applyEasingSafe(ScreenEnterElapsedMs / kScreenEnterMs, Easing::EaseOutCubic);
    out = UiMotionSample{};
    out.alpha = (float)P;
    out.offY = 10.f * (float)(1.0 - P);   // わずかに下から（論理px）
    return true;
}

int UiMotionState::activeCount() const
{
    int N = 0;
    if (ScreenEnterActive) ++N;
    for (const auto& [K, E] : Entries)
        if (E.enterActive || E.countActive || E.pulseActive || E.spec.hasLoop() || E.exitActive || E.tweenActive) ++N;
    return N;
}

std::vector<std::string> UiMotionState::drainLogs()
{
    std::vector<std::string> Out;
    Out.swap(Logs);
    return Out;
}

}  // namespace kaedevn::ui
