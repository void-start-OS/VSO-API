// src/vso/console.cpp
// 実装: vso::console
// - クロスプラットフォーム（Windows / POSIX）での堅牢なコンソール出力ラッパ
// - スレッドセーフ、引数検証、最小限の動的割当て、エラーコードを明確に返す
// - ANSI エスケープシーケンスを優先（Windows 10+ では有効化を試みる）
// - ヘッダ: include/vso/console.hpp を前提

#include "vso/console.hpp"
#include "vso/types.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <atomic>

#if defined(_WIN32) || defined(_WIN64)
#  define VSO_PLATFORM_WINDOWS 1
#  include <windows.h>
#else
#  define VSO_PLATFORM_POSIX 1
#  include <unistd.h>
#endif

namespace vso::console {

namespace {

// グローバルロック：複数スレッドからの同時出力を直列化する
static std::mutex g_console_mutex;

// Windows: コンソールハンドルと ANSI 有効化フラグ
#if defined(VSO_PLATFORM_WINDOWS)
static std::atomic<bool> g_windows_initialized{false};
static std::atomic<bool> g_windows_ansi_enabled{false};
static HANDLE g_stdout_handle = INVALID_HANDLE_VALUE;
static WORD g_original_attributes = 0;

// Windows コンソールで ANSI エスケープを有効化する（可能なら）
// 失敗してもフォールバックで WinAPI の SetConsoleTextAttribute を使う
inline void windows_init_once() noexcept {
    if (g_windows_initialized.load(std::memory_order_acquire)) return;
    // 初期化は単純化のためロックレスで一度だけ行う
    g_stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_stdout_handle == INVALID_HANDLE_VALUE || g_stdout_handle == nullptr) {
        g_windows_initialized.store(true, std::memory_order_release);
        g_windows_ansi_enabled.store(false, std::memory_order_release);
        return;
    }

    // 保存しておく既存属性（失敗しても無視）
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_stdout_handle, &csbi)) {
        g_original_attributes = csbi.wAttributes;
    }

    // Try to enable virtual terminal processing (ANSI) on Windows 10+
    DWORD mode = 0;
    if (GetConsoleMode(g_stdout_handle, &mode)) {
        DWORD newMode = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        if (SetConsoleMode(g_stdout_handle, newMode)) {
            g_windows_ansi_enabled.store(true, std::memory_order_release);
        } else {
            g_windows_ansi_enabled.store(false, std::memory_order_release);
        }
    } else {
        g_windows_ansi_enabled.store(false, std::memory_order_release);
    }

    g_windows_initialized.store(true, std::memory_order_release);
}

// Windows: ConsoleColor -> ANSI シーケンス / WinAPI 属性マッピング
inline const char* ansi_for_color(ConsoleColor c) noexcept {
    switch (c) {
    case ConsoleColor::Default:     return "\x1b[0m";
    case ConsoleColor::Black:       return "\x1b[30m";
    case ConsoleColor::Blue:        return "\x1b[34m";
    case ConsoleColor::Green:       return "\x1b[32m";
    case ConsoleColor::Cyan:        return "\x1b[36m";
    case ConsoleColor::Red:         return "\x1b[31m";
    case ConsoleColor::Magenta:     return "\x1b[35m";
    case ConsoleColor::Brown:       return "\x1b[33m"; // brown -> yellow-ish
    case ConsoleColor::LightGray:   return "\x1b[37m";
    case ConsoleColor::DarkGray:    return "\x1b[90m";
    case ConsoleColor::LightBlue:   return "\x1b[94m";
    case ConsoleColor::LightGreen:  return "\x1b[92m";
    case ConsoleColor::LightCyan:   return "\x1b[96m";
    case ConsoleColor::LightRed:    return "\x1b[91m";
    case ConsoleColor::LightMagenta:return "\x1b[95m";
    case ConsoleColor::Yellow:      return "\x1b[33m";
    case ConsoleColor::White:       return "\x1b[97m";
    default:                        return "\x1b[0m";
    }
}

inline WORD winattr_for_color(ConsoleColor c) noexcept {
    // Windows コンソール属性は FOREGROUND_* ビットで表現
    switch (c) {
    case ConsoleColor::Default:     return g_original_attributes;
    case ConsoleColor::Black:       return 0;
    case ConsoleColor::Blue:        return FOREGROUND_BLUE;
    case ConsoleColor::Green:       return FOREGROUND_GREEN;
    case ConsoleColor::Cyan:        return FOREGROUND_GREEN | FOREGROUND_BLUE;
    case ConsoleColor::Red:         return FOREGROUND_RED;
    case ConsoleColor::Magenta:     return FOREGROUND_RED | FOREGROUND_BLUE;
    case ConsoleColor::Brown:       return FOREGROUND_RED | FOREGROUND_GREEN; // yellow-ish
    case ConsoleColor::LightGray:   return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    case ConsoleColor::DarkGray:    return FOREGROUND_INTENSITY;
    case ConsoleColor::LightBlue:   return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case ConsoleColor::LightGreen:  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case ConsoleColor::LightCyan:   return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case ConsoleColor::LightRed:    return FOREGROUND_RED | FOREGROUND_INTENSITY;
    case ConsoleColor::LightMagenta:return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case ConsoleColor::Yellow:      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case ConsoleColor::White:       return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    default:                        return g_original_attributes;
    }
}
#endif // Windows

// POSIX: ConsoleColor -> ANSI シーケンス
#if defined(VSO_PLATFORM_POSIX)
inline const char* ansi_for_color(ConsoleColor c) noexcept {
    switch (c) {
    case ConsoleColor::Default:     return "\x1b[0m";
    case ConsoleColor::Black:       return "\x1b[30m";
    case ConsoleColor::Blue:        return "\x1b[34m";
    case ConsoleColor::Green:       return "\x1b[32m";
    case ConsoleColor::Cyan:        return "\x1b[36m";
    case ConsoleColor::Red:         return "\x1b[31m";
    case ConsoleColor::Magenta:     return "\x1b[35m";
    case ConsoleColor::Brown:       return "\x1b[33m";
    case ConsoleColor::LightGray:   return "\x1b[37m";
    case ConsoleColor::DarkGray:    return "\x1b[90m";
    case ConsoleColor::LightBlue:   return "\x1b[94m";
    case ConsoleColor::LightGreen:  return "\x1b[92m";
    case ConsoleColor::LightCyan:   return "\x1b[96m";
    case ConsoleColor::LightRed:    return "\x1b[91m";
    case ConsoleColor::LightMagenta:return "\x1b[95m";
    case ConsoleColor::Yellow:      return "\x1b[33m";
    case ConsoleColor::White:       return "\x1b[97m";
    default:                        return "\x1b[0m";
    }
}
#endif // POSIX

// 内部: 安全に文字列を出力する（バッファ化して fwrite）
// - text が nullptr の場合は InvalidArgument を返す
// - 書き込み先は stdout（将来的に選択可能）
// - スレッドセーフにするため外部で mutex を取る
inline Result write_to_stdout(const char* text, size_t len) noexcept {
    if (text == nullptr) return vso::results::InvalidArgument;
    if (len == 0) return vso::results::Ok;

#if defined(VSO_PLATFORM_WINDOWS)
    // Windows: fwrite は内部でバッファリングされる。ここでは標準出力に書く。
    size_t written = std::fwrite(text, 1, len, stdout);
    if (written != len) return vso::results::Failed;
    // 明示的に flush しない（パフォーマンス優先）。必要なら呼び出し側で fflush する。
    return vso::results::Ok;
#else
    // POSIX: write を使って直接書き込む（スレッドセーフで部分書き込みを扱う）
    ssize_t r = ::write(STDOUT_FILENO, text, static_cast<size_t>(len));
    if (r < 0) return vso::results::Failed;
    // r may be less than len; loop until all written
    size_t total = static_cast<size_t>(r);
    const char* ptr = text + total;
    size_t remaining = len - total;
    while (remaining > 0) {
        ssize_t rr = ::write(STDOUT_FILENO, ptr, remaining);
        if (rr < 0) return vso::results::Failed;
        ptr += rr;
        remaining -= static_cast<size_t>(rr);
    }
    return vso::results::Ok;
#endif
}

} // namespace (internal)

// Public API

Result Write(const char* text) {
    if (text == nullptr) return vso::results::InvalidArgument;

    std::lock_guard<std::mutex> lock(g_console_mutex);

#if defined(VSO_PLATFORM_WINDOWS)
    windows_init_once();
    if (g_windows_ansi_enabled.load(std::memory_order_acquire)) {
        // ANSI が有効ならそのまま出力
        size_t len = std::strlen(text);
        return write_to_stdout(text, len);
    } else {
        // ANSI が無効な場合も通常文字列は問題なく出力できる
        size_t len = std::strlen(text);
        return write_to_stdout(text, len);
    }
#else
    size_t len = std::strlen(text);
    return write_to_stdout(text, len);
#endif
}

Result WriteLine(const char* text) {
    if (text == nullptr) return vso::results::InvalidArgument;

    std::lock_guard<std::mutex> lock(g_console_mutex);

    // 出力文字列 + 改行（LF）
#if defined(VSO_PLATFORM_WINDOWS)
    windows_init_once();
    if (g_windows_ansi_enabled.load(std::memory_order_acquire)) {
        // ANSI enabled: write text then newline
        size_t len = std::strlen(text);
        Result r = write_to_stdout(text, len);
        if (r.is_error()) return r;
        return write_to_stdout("\n", 1);
    } else {
        // ANSI disabled: write text then CRLF to match Windows convention
        size_t len = std::strlen(text);
        Result r = write_to_stdout(text, len);
        if (r.is_error()) return r;
        return write_to_stdout("\r\n", 2);
    }
#else
    size_t len = std::strlen(text);
    Result r = write_to_stdout(text, len);
    if (r.is_error()) return r;
    return write_to_stdout("\n", 1);
#endif
}

Result Clear() {
    std::lock_guard<std::mutex> lock(g_console_mutex);

#if defined(VSO_PLATFORM_WINDOWS)
    windows_init_once();
    if (g_stdout_handle == INVALID_HANDLE_VALUE || g_stdout_handle == nullptr) {
        // フォールバック: ANSI シーケンスでクリアを試みる
        const char* seq = "\x1b[2J\x1b[H";
        return write_to_stdout(seq, std::strlen(seq));
    }

    // WinAPI を使って画面をクリアする（より確実）
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(g_stdout_handle, &csbi)) {
        // フォールバック: ANSI
        const char* seq = "\x1b[2J\x1b[H";
        return write_to_stdout(seq, std::strlen(seq));
    }

    DWORD consoleSize = csbi.dwSize.X * csbi.dwSize.Y;
    COORD topLeft = { 0, 0 };
    DWORD charsWritten = 0;

    // Fill with spaces
    if (!FillConsoleOutputCharacterA(g_stdout_handle, ' ', consoleSize, topLeft, &charsWritten)) {
        const char* seq = "\x1b[2J\x1b[H";
        return write_to_stdout(seq, std::strlen(seq));
    }

    // Reset attributes
    if (!FillConsoleOutputAttribute(g_stdout_handle, csbi.wAttributes, consoleSize, topLeft, &charsWritten)) {
        const char* seq = "\x1b[2J\x1b[H";
        return write_to_stdout(seq, std::strlen(seq));
    }

    // Move cursor to top-left
    SetConsoleCursorPosition(g_stdout_handle, topLeft);
    return vso::results::Ok;
#else
    // POSIX: ANSI シーケンスで画面クリアとカーソル移動
    const char* seq = "\x1b[2J\x1b[H";
    return write_to_stdout(seq, std::strlen(seq));
#endif
}

Result SetTextColor(ConsoleColor color) {
    std::lock_guard<std::mutex> lock(g_console_mutex);

#if defined(VSO_PLATFORM_WINDOWS)
    windows_init_once();
    if (g_windows_ansi_enabled.load(std::memory_order_acquire)) {
        // ANSI enabled: 出力に ANSI シーケンスを書き込む
        const char* seq = ansi_for_color(color);
        return write_to_stdout(seq, std::strlen(seq));
    } else {
        // ANSI 無効: WinAPI で属性を設定
        if (g_stdout_handle == INVALID_HANDLE_VALUE || g_stdout_handle == nullptr) {
            // フォールバック: ANSI シーケンス
            const char* seq = ansi_for_color(color);
            return write_to_stdout(seq, std::strlen(seq));
        }
        WORD attr = winattr_for_color(color);
        if (!SetConsoleTextAttribute(g_stdout_handle, attr)) {
            // 失敗したら ANSI を試す
            const char* seq = ansi_for_color(color);
            return write_to_stdout(seq, std::strlen(seq));
        }
        return vso::results::Ok;
    }
#else
    // POSIX: ANSI シーケンスを出力
    const char* seq = ansi_for_color(color);
    return write_to_stdout(seq, std::strlen(seq));
#endif
}

} // namespace vso::console
