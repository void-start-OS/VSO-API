// src/vso/time.cpp
// 実装: vso::time
// 高精度かつ堅牢な Sleep / GetTickCount 実装
// - クロスプラットフォーム（標準C++のみ）で動作
// - オーバーフロー・引数検証・初期化の安全性を確保
// - ヘッダ: include/vso/time.hpp を前提

#include "vso/time.hpp"

#include "vso/types.hpp"

#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

namespace vso::time {

// 内部: モノトニック基準時刻（起動時刻）
// - std::chrono::steady_clock を使用して、システム時刻の変更に影響されない経過時間を提供する。
// - スレッドセーフな遅延初期化を行う。
namespace {
    using steady_clock = std::chrono::steady_clock;
    using ms_t = std::chrono::milliseconds;

    // 起動基準時刻（モジュールロード時に初期化される）
    static std::atomic<bool> g_initialized{false};
    static steady_clock::time_point g_start_time;
    static std::once_flag g_init_flag;

    // 初期化ルーチン（スレッドセーフ）
    inline void ensure_initialized() noexcept {
        std::call_once(g_init_flag, []() noexcept {
            g_start_time = steady_clock::now();
            g_initialized.store(true, std::memory_order_release);
        });
    }

    // Sleep の内部最大チャンク（長時間スリープを分割することで
    // 実装依存の制約や将来の割り込み/キャンセル実装を容易にする）
    // 1秒単位で分割する（任意の値に調整可能）
    constexpr std::uint32_t SLEEP_CHUNK_MS = 1000u;
}

// 指定ミリ秒スリープ
// - 引数検証: milliseconds は u32 のため負値は来ないが、0 は即時復帰。
// - 実装は std::this_thread::sleep_for を用いる。長時間スリープはチャンク分割して実行。
// - 戻り値: 成功時 vso::results::Ok、引数不正時 vso::results::InvalidArgument
Result Sleep(u32 milliseconds) {
    // u32 の範囲は 0 .. 4294967295 (約49.7日)
    // 0 は即時復帰
    ensure_initialized();

    // 明示的な検証（将来仕様変更で負値型が来た場合に備える）
    // ここでは u32 なので常に >= 0。だが念のためチェックしておく。
    // （API の一貫性のため、0 は Ok を返す）
    if (milliseconds == 0u) {
        return vso::results::Ok;
    }

    // チャンク分割してスリープ（大きな値でも安全）
    u64 remaining = static_cast<u64>(milliseconds);
    while (remaining > 0) {
        u32 chunk = (remaining > SLEEP_CHUNK_MS) ? SLEEP_CHUNK_MS : static_cast<u32>(remaining);
        // std::chrono::milliseconds は signed 型を取る実装が多いが、
        // chunk は最大 1000 なので安全にキャストできる。
        std::this_thread::sleep_for(ms_t(static_cast<std::int64_t>(chunk)));
        remaining -= chunk;
    }

    return vso::results::Ok;
}

// 起動後経過時間（ミリ秒）を返す
// - out 引数に TickCount を書き込む（nullptr は InvalidArgument）
// - steady_clock を基準にしているため、システム時刻の変更に影響されない。
// - ミリ秒は u64 で返す（長時間稼働でもオーバーフローしにくい）
Result GetTickCount(TickCount* out) {
    if (out == nullptr) return vso::results::InvalidArgument;

    ensure_initialized();

    // steady_clock::now() - g_start_time をミリ秒に変換
    auto now = steady_clock::now();
    auto dur = std::chrono::duration_cast<ms_t>(now - g_start_time);
    // duration_cast の結果は signed 型の可能性があるため、負値防止
    if (dur.count() < 0) {
        // 理論上発生しないが、安全のため 0 を返す
        out->milliseconds = 0ull;
        return vso::results::Ok;
    }

    // dur.count() は 64bit にキャストしても安全（steady_clock の精度に依存）
    out->milliseconds = static_cast<u64>(dur.count());
    return vso::results::Ok;
}

} // namespace vso::time
