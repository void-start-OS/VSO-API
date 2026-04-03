// src/vso/system.cpp
// 実装: vso::system
// - クロスプラットフォームで可能な限り正確なシステム情報を取得する実装
// - Reboot は「戻らない」ことを保証するための最善努力実装（プラットフォーム依存の権限が必要）
// - GetSystemInfo は可能な限り堅牢に情報を埋め、失敗時は適切な Result を返す
// - 実装は標準ライブラリと POSIX / Windows API のみを使用
// - ヘッダ: include/vso/system.hpp を前提

#include "vso/system.hpp"
#include "vso/types.hpp"

#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <system_error>

#if defined(_WIN32) || defined(_WIN64)
#  define VSO_PLATFORM_WINDOWS 1
#  include <windows.h>
#  include <processthreadsapi.h>
#  include <synchapi.h>
#else
#  define VSO_PLATFORM_POSIX 1
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/statvfs.h>
#  include <sys/utsname.h>
#  include <sys/time.h>
#  include <sys/resource.h>
#  include <fcntl.h>
#  include <errno.h>
#  if defined(__linux__)
#    include <sys/sysinfo.h>
#    include <sys/reboot.h>
#    include <linux/reboot.h>
#    include <sys/syscall.h>
#  endif
#endif

namespace vso::system {

namespace {

// Helper: safe cast to u64 from signed/unsigned integral types
template <typename T>
constexpr u64 safe_to_u64(T v) noexcept {
    if constexpr (std::is_signed_v<T>) {
        if (v < 0) return 0ull;
        return static_cast<u64>(v);
    } else {
        return static_cast<u64>(v);
    }
}

// Helper: get logical CPU count with robust fallback
u32 query_cpu_count() noexcept {
    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0u) {
#if defined(VSO_PLATFORM_POSIX)
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n > 0) return static_cast<u32>(n);
#endif
        // 最低 1 を返す
        return 1u;
    }
    return static_cast<u32>(hc);
}

#if defined(VSO_PLATFORM_WINDOWS)

// Windows: Get memory info via GlobalMemoryStatusEx
Result query_memory_windows(u64& total_out, u64& used_out) noexcept {
    MEMORYSTATUSEX ms;
    std::memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        return vso::results::Failed;
    }
    // ms.ullTotalPhys, ms.ullAvailPhys
    total_out = static_cast<u64>(ms.ullTotalPhys);
    u64 avail = static_cast<u64>(ms.ullAvailPhys);
    used_out = (total_out >= avail) ? (total_out - avail) : 0ull;
    return vso::results::Ok;
}

#elif defined(__linux__)

// Linux: use sysinfo if available
Result query_memory_linux(u64& total_out, u64& used_out) noexcept {
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        return vso::results::Failed;
    }
    // si.totalram and si.freeram are in units of bytes * si.mem_unit
    u64 unit = static_cast<u64>(si.mem_unit);
    total_out = safe_to_u64(si.totalram) * unit;
    u64 free_all = safe_to_u64(si.freeram) * unit;
    // sysinfo provides buffers/cache in si.bufferram and si.sharedram; approximate used = total - free - buff/cache
    // Some kernels expose si.bufferram; if not, we approximate used = total - free
    u64 buffers = safe_to_u64(si.bufferram) * unit;
    // Note: this is an approximation; for precise values parse /proc/meminfo if needed.
    if (total_out >= (free_all + buffers)) {
        used_out = total_out - free_all - buffers;
    } else if (total_out >= free_all) {
        used_out = total_out - free_all;
    } else {
        used_out = 0ull;
    }
    return vso::results::Ok;
}

// Fallback: parse /proc/meminfo for more accurate used memory (Linux-specific)
Result query_memory_linux_proc(u64& total_out, u64& used_out) noexcept {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return vso::results::Failed;
    char line[256];
    u64 mem_total = 0ull;
    u64 mem_free = 0ull;
    u64 buffers = 0ull;
    u64 cached = 0ull;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "MemTotal: %llu kB", &mem_total) == 1) continue;
        if (std::sscanf(line, "MemFree: %llu kB", &mem_free) == 1) continue;
        if (std::sscanf(line, "Buffers: %llu kB", &buffers) == 1) continue;
        if (std::sscanf(line, "Cached: %llu kB", &cached) == 1) continue;
    }
    std::fclose(f);
    if (mem_total == 0ull) return vso::results::Failed;
    total_out = mem_total * 1024ull;
    // used = total - free - buffers - cached
    u64 free_all = mem_free * 1024ull;
    u64 buff_bytes = buffers * 1024ull;
    u64 cached_bytes = cached * 1024ull;
    if (total_out >= (free_all + buff_bytes + cached_bytes)) {
        used_out = total_out - free_all - buff_bytes - cached_bytes;
    } else if (total_out >= free_all) {
        used_out = total_out - free_all;
    } else {
        used_out = 0ull;
    }
    return vso::results::Ok;
}

#elif defined(__APPLE__)

// macOS: use sysctl and host_statistics
#  include <sys/sysctl.h>
#  include <mach/mach.h>

Result query_memory_macos(u64& total_out, u64& used_out) noexcept {
    int mib[2];
    int64_t physical_memory = 0;
    size_t length = sizeof(physical_memory);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    if (sysctl(mib, 2, &physical_memory, &length, nullptr, 0) != 0) {
        return vso::results::Failed;
    }
    total_out = static_cast<u64>(physical_memory);

    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmstat), &count) != KERN_SUCCESS) {
        return vso::results::Failed;
    }
    u64 page_size = static_cast<u64>(vm_page_size);
    u64 free_bytes = static_cast<u64>(vmstat.free_count) * page_size;
    u64 inactive_bytes = static_cast<u64>(vmstat.inactive_count) * page_size;
    u64 speculative_bytes = 0;
#if defined(VM_STATISTICS64)
    // speculative may not be present on all macOS versions; ignore if not available
#endif
    // used = total - (free + inactive)
    if (total_out >= (free_bytes + inactive_bytes + speculative_bytes)) {
        used_out = total_out - free_bytes - inactive_bytes - speculative_bytes;
    } else if (total_out >= free_bytes) {
        used_out = total_out - free_bytes;
    } else {
        used_out = 0ull;
    }
    return vso::results::Ok;
}

#endif // platform-specific memory queries

} // namespace (internal)

// Reboot: [[noreturn]] 実装
[[noreturn]] void Reboot() {
    // 最初に可能な限りの同期処理を行う（ファイルシステムの整合性確保）
    // ここでは標準的な sync を呼び出す（POSIX）や FlushFileBuffers（Windows）を試みる
#if defined(VSO_PLATFORM_WINDOWS)
    // Flush all file buffers for standard handles if possible
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(hStdOut);
    }
    // Try to initiate a system reboot. This requires the calling process to have the SE_SHUTDOWN_NAME privilege.
    // We attempt to enable the privilege; if it fails, fall back to best-effort termination.
    HANDLE hToken = nullptr;
    TOKEN_PRIVILEGES tkp;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
        if (GetLastError() == ERROR_SUCCESS) {
            // Request reboot
            if (ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_SOFTWARE | SHTDN_REASON_FLAG_PLANNED)) {
                // If successful, the system will reboot and this process will be terminated.
                // But in case it returns, fall through to termination loop.
            }
        }
        CloseHandle(hToken);
    }
    // If we reach here, reboot attempt failed or privileges insufficient.
    // As a last resort, try to call InitiateSystemShutdownEx if available (may still fail).
    // Finally, ensure we never return: terminate process and spin.
    std::fflush(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Forceful termination
    std::terminate();
    for (;;) { std::this_thread::sleep_for(std::chrono::hours(24)); }
#elif defined(VSO_PLATFORM_POSIX)
    // POSIX: sync filesystem buffers
    ::sync();

#  if defined(__linux__)
    // Try to call reboot syscall (requires CAP_SYS_BOOT)
    // Use syscall to avoid glibc wrapper requiring extra privileges
    // LINUX_REBOOT_CMD_RESTART is the usual restart command
    // Note: syscall may fail if not privileged; ignore return and fall back.
    syscall(SYS_reboot, LINUX_REBOOT_CMD_RESTART);
#  endif

    // Try to execute /sbin/reboot or /usr/sbin/reboot if available (best-effort)
    // Use _exit after fork/exec attempt to avoid returning to caller.
    pid_t pid = fork();
    if (pid == 0) {
        // child
        execl("/sbin/reboot", "reboot", nullptr);
        execl("/usr/sbin/reboot", "reboot", nullptr);
        // If exec fails, exit child
        _exit(127);
    } else if (pid > 0) {
        // parent: wait a short while for child to take effect
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } else {
        // fork failed; continue to fallback
    }

    // If we reach here, reboot did not occur (likely due to insufficient privileges).
    // Ensure we never return: flush and _exit with non-zero code.
    std::fflush(nullptr);
    _exit(1);
#else
    // Unknown platform: best-effort terminate
    std::fflush(nullptr);
    std::terminate();
    for (;;) { std::this_thread::sleep_for(std::chrono::hours(24)); }
#endif
}

// GetSystemInfo: 実装
Result GetSystemInfo(SystemInfo& outInfo) {
    // 初期値をゼロで埋める（呼び出し側が未初期化のまま使うことを防ぐ）
    outInfo.totalMemoryBytes = 0ull;
    outInfo.usedMemoryBytes = 0ull;
    outInfo.cpuCount = 0u;

    // CPU count
    outInfo.cpuCount = query_cpu_count();

    // Memory: プラットフォームごとに最善の方法で取得
#if defined(VSO_PLATFORM_WINDOWS)
    u64 total = 0ull, used = 0ull;
    Result r = query_memory_windows(total, used);
    if (r.is_ok()) {
        outInfo.totalMemoryBytes = total;
        outInfo.usedMemoryBytes = used;
        return results::Ok;
    } else {
        return results::Failed;
    }
#elif defined(__linux__)
    u64 total = 0ull, used = 0ull;
    // まず sysinfo を試す
    if (query_memory_linux(total, used).is_ok()) {
        // さらに /proc/meminfo を試して精度を上げる（成功すれば上書き）
        u64 total2 = 0ull, used2 = 0ull;
        if (query_memory_linux_proc(total2, used2).is_ok()) {
            outInfo.totalMemoryBytes = total2;
            outInfo.usedMemoryBytes = used2;
            return results::Ok;
        } else {
            outInfo.totalMemoryBytes = total;
            outInfo.usedMemoryBytes = used;
            return results::Ok;
        }
    } else {
        // 最後の手段として /proc/meminfo を試す
        if (query_memory_linux_proc(total, used).is_ok()) {
            outInfo.totalMemoryBytes = total;
            outInfo.usedMemoryBytes = used;
            return results::Ok;
        }
        return results::Failed;
    }
#elif defined(__APPLE__)
    u64 total = 0ull, used = 0ull;
    if (query_memory_macos(total, used).is_ok()) {
        outInfo.totalMemoryBytes = total;
        outInfo.usedMemoryBytes = used;
        return results::Ok;
    }
    return results::Failed;
#else
    // Unknown POSIX-like: try sysconf
#  if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        outInfo.totalMemoryBytes = static_cast<u64>(pages) * static_cast<u64>(page_size);
        // usedMemory unknown; leave as 0
        return results::Ok;
    }
#  endif
    return results::NotImplemented;
#endif
}

} // namespace vso::system
