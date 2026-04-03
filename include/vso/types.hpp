// include/vso/core_types.hpp
#pragma once

// VSOAPI Core Types and Error Infrastructure
// Single-file, high-performance, ABI-stable foundational header
// Designed for kernel/user boundary usage and public API exposure

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <limits>
#include <chrono>
#include <string>
#include <string_view>
#include <optional>
#include <array>
#include <cassert>
#include <cstring>
#include <utility>

namespace vso {

// -----------------------------
// Fundamental fixed-width aliases
// -----------------------------
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

static_assert(sizeof(u8)  == 1, "u8 size");
static_assert(sizeof(u16) == 2, "u16 size");
static_assert(sizeof(u32) == 4, "u32 size");
static_assert(sizeof(u64) == 8, "u64 size");

// Backwards compatibility aliases (explicitly documented ABI)
using DWORD = u32;
using QWORD = u64;

// BOOL kept for compatibility but strongly discouraged in new APIs.
// Use bool for logical values; use Result for error semantics.
using BOOL = bool;

// -----------------------------
// Low-overhead span (C++20 std::span recommended)
// -----------------------------
template <typename T>
using Span = std::span<T>;

// -----------------------------
// Time primitives
// -----------------------------
struct TickCount {
    u64 milliseconds;

    constexpr TickCount() noexcept : milliseconds(0) {}
    explicit constexpr TickCount(u64 ms) noexcept : milliseconds(ms) {}

    static constexpr TickCount from_seconds(u64 s) noexcept { return TickCount(s * 1000ull); }
    static constexpr TickCount from_milliseconds(u64 ms) noexcept { return TickCount(ms); }

    constexpr u64 to_milliseconds() const noexcept { return milliseconds; }
    constexpr double to_seconds_f() const noexcept { return static_cast<double>(milliseconds) / 1000.0; }

    constexpr TickCount operator+(TickCount o) const noexcept { return TickCount(milliseconds + o.milliseconds); }
    constexpr TickCount operator-(TickCount o) const noexcept { return TickCount(milliseconds - o.milliseconds); }
    constexpr bool operator==(TickCount o) const noexcept { return milliseconds == o.milliseconds; }
    constexpr bool operator!=(TickCount o) const noexcept { return milliseconds != o.milliseconds; }
    constexpr bool operator<(TickCount o) const noexcept { return milliseconds < o.milliseconds; }
    constexpr bool operator>(TickCount o) const noexcept { return milliseconds > o.milliseconds; }
};

// -----------------------------
// Error / Result system
// - 32-bit packed: [facility:8][severity:2][reserved:6][code:16]
// - facility: subsystem identifier
// - severity: 0=Ok,1=Info,2=Warn,3=Error
// - code: subsystem-specific code
// -----------------------------
namespace detail {
    constexpr u32 make_packed(u8 facility, u8 severity, u16 code) noexcept {
        return (static_cast<u32>(facility) << 24) |
               (static_cast<u32>(severity & 0x3u) << 22) |
               (static_cast<u32>(code) & 0xFFFFu);
    }
    constexpr u8 extract_facility(u32 v) noexcept { return static_cast<u8>((v >> 24) & 0xFFu); }
    constexpr u8 extract_severity(u32 v) noexcept { return static_cast<u8>((v >> 22) & 0x3u); }
    constexpr u16 extract_code(u32 v) noexcept { return static_cast<u16>(v & 0xFFFFu); }
}

// Facility identifiers
enum class Facility : u8 {
    Core        = 0x01,
    Memory      = 0x02,
    FileSystem  = 0x03,
    Process     = 0x04,
    IPC         = 0x05,
    Network     = 0x06,
    Driver      = 0x07,
    Security    = 0x08,
    Unknown     = 0xFF,
};

// Severity levels
enum class Severity : u8 {
    Ok   = 0,
    Info = 1,
    Warn = 2,
    Err  = 3,
};

// Result type: lightweight, trivially copyable, ABI-friendly
struct Result {
    u32 value;

    constexpr Result() noexcept : value(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Ok), 0u)) {}
    explicit constexpr Result(u32 v) noexcept : value(v) {}

    static constexpr Result Ok() noexcept { return Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Ok), 0u)); }
    static constexpr Result Failed() noexcept { return Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 1u)); }

    constexpr bool is_ok() const noexcept { return detail::extract_severity(value) == static_cast<u8>(Severity::Ok); }
    constexpr bool is_error() const noexcept { return detail::extract_severity(value) == static_cast<u8>(Severity::Err); }
    constexpr Facility facility() const noexcept { return static_cast<Facility>(detail::extract_facility(value)); }
    constexpr Severity severity() const noexcept { return static_cast<Severity>(detail::extract_severity(value)); }
    constexpr u16 code() const noexcept { return detail::extract_code(value); }

    explicit constexpr operator bool() const noexcept { return is_ok(); }

    constexpr bool operator==(Result o) const noexcept { return value == o.value; }
    constexpr bool operator!=(Result o) const noexcept { return value != o.value; }
};

// Predefined common results for Core facility
namespace results {
    constexpr Result Ok              = Result::Ok();
    constexpr Result Failed          = Result::Failed();
    constexpr Result InvalidArgument = Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 2u));
    constexpr Result NotFound        = Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 3u));
    constexpr Result OutOfMemory     = Result(detail::make_packed(static_cast<u8>(Facility::Memory), static_cast<u8>(Severity::Err), 1u));
    constexpr Result NotImplemented  = Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 4u));
    constexpr Result BufferTooSmall  = Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 5u));
    constexpr Result PermissionDenied= Result(detail::make_packed(static_cast<u8>(Facility::Security), static_cast<u8>(Severity::Err), 1u));
    constexpr Result Timeout         = Result(detail::make_packed(static_cast<u8>(Facility::Core), static_cast<u8>(Severity::Err), 6u));
}

// Utility to construct Result from components
constexpr Result make_result(Facility f, Severity s, u16 code) noexcept {
    return Result(detail::make_packed(static_cast<u8>(f), static_cast<u8>(s), code));
}

// Convert Result to human-readable string (no heap allocation)
inline std::string_view result_to_string_view(Result r) noexcept {
    // Minimal mapping for common codes; callers may map facility-specific codes.
    if (r == results::Ok) return "Ok";
    if (r == results::Failed) return "Failed";
    if (r == results::InvalidArgument) return "InvalidArgument";
    if (r == results::NotFound) return "NotFound";
    if (r == results::OutOfMemory) return "OutOfMemory";
    if (r == results::NotImplemented) return "NotImplemented";
    if (r == results::BufferTooSmall) return "BufferTooSmall";
    if (r == results::PermissionDenied) return "PermissionDenied";
    if (r == results::Timeout) return "Timeout";
    return "UnknownResult";
}

// -----------------------------
// Checked integer helpers
// -----------------------------
template <typename T>
struct Checked {
    static_assert(std::is_integral_v<T>, "Checked requires integral type");
    T value;

    constexpr Checked() noexcept : value(0) {}
    constexpr explicit Checked(T v) noexcept : value(v) {}

    constexpr bool add_overflow(T rhs, T &out) const noexcept {
        if constexpr (std::is_unsigned_v<T>) {
            out = value + rhs;
            return out < value;
        } else {
            using U = std::make_unsigned_t<T>;
            U a = static_cast<U>(value);
            U b = static_cast<U>(rhs);
            U r = a + b;
            out = static_cast<T>(r);
            if ((rhs > 0 && out < value) || (rhs < 0 && out > value)) return true;
            return false;
        }
    }
};

// -----------------------------
// Small, zero-overhead optional view for C ABI interop
// -----------------------------
template <typename T>
struct OptionalRef {
    T* ptr;

    constexpr OptionalRef() noexcept : ptr(nullptr) {}
    constexpr OptionalRef(std::nullptr_t) noexcept : ptr(nullptr) {}
    constexpr explicit OptionalRef(T* p) noexcept : ptr(p) {}

    constexpr bool has_value() const noexcept { return ptr != nullptr; }
    constexpr T& operator*() const noexcept { assert(ptr); return *ptr; }
    constexpr T* operator->() const noexcept { assert(ptr); return ptr; }
};

// -----------------------------
// Simple, fast string utilities (no heap where possible)
// -----------------------------
inline bool str_equal_ci(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

// -----------------------------
// Validation helpers
// -----------------------------
inline Result validate_pointer(const void* p) noexcept {
    if (p == nullptr) return results::InvalidArgument;
    return results::Ok;
}

inline Result validate_range(usize offset, usize length, usize capacity) noexcept {
    if (offset > capacity) return results::InvalidArgument;
    if (length > capacity) return results::InvalidArgument;
    if (offset + length > capacity) return results::InvalidArgument;
    return results::Ok;
}

// -----------------------------
// Lightweight buffer writer (no heap, stack-friendly)
// -----------------------------
struct BufferWriter {
    char* buf;
    usize capacity;
    usize pos;

    constexpr BufferWriter(char* b, usize cap) noexcept : buf(b), capacity(cap), pos(0) {}

    constexpr usize remaining() const noexcept { return capacity - pos; }

    Result write(const char* src, usize len) noexcept {
        if (len > remaining()) return results::BufferTooSmall;
        std::memcpy(buf + pos, src, len);
        pos += len;
        return results::Ok;
    }

    Result write_char(char c) noexcept {
        if (remaining() < 1) return results::BufferTooSmall;
        buf[pos++] = c;
        return results::Ok;
    }

    Result write_str_view(std::string_view sv) noexcept {
        return write(sv.data(), sv.size());
    }

    void reset() noexcept { pos = 0; }
};

// -----------------------------
// Safe handle pattern (32-bit opaque handle)
// -----------------------------
using Handle = u32;
constexpr Handle InvalidHandle = 0u;

struct HandleTraits {
    static constexpr Handle make(u32 index, u16 generation) noexcept {
        return (static_cast<u32>(generation) << 24) | (index & 0x00FFFFFFu);
    }
    static constexpr u32 index(Handle h) noexcept { return h & 0x00FFFFFFu; }
    static constexpr u16 generation(Handle h) noexcept { return static_cast<u16>((h >> 24) & 0xFFu); }
    static constexpr bool is_valid(Handle h) noexcept { return h != InvalidHandle; }
};

// -----------------------------
// Minimal lock-free atomic flag (C++20 atomic_flag recommended in implementation files)
// -----------------------------
struct AtomicFlag {
    alignas(4) u32 state;

    constexpr AtomicFlag() noexcept : state(0u) {}

    bool test_and_set() noexcept {
        // Placeholder: platform-specific atomic required in implementation
        // For header-only safe default, use simple non-atomic (caller must ensure synchronization)
        if (state == 0u) { state = 1u; return false; }
        return true;
    }
    void clear() noexcept { state = 0u; }
};

// -----------------------------
// Public API validation macros (lightweight, no exceptions)
// -----------------------------
#define VSO_CHECK_PTR(p) do { if ((p) == nullptr) return vso::results::InvalidArgument; } while(0)
#define VSO_CHECK_RANGE(off,len,cap) do { if (vso::validate_range((off),(len),(cap)).is_error()) return vso::results::InvalidArgument; } while(0)
#define VSO_RETURN_ON_ERROR(r) do { auto __r = (r); if ((__r).is_error()) return (__r); } while(0)

// -----------------------------
// Example Core API surface (robust, validated, minimal allocations)
// - This is a foundational example showing patterns to follow across VSOAPI.
// - Implementations should be in .cpp files; header exposes inline, constexpr, and ABI-safe types.
// -----------------------------
namespace core {

// Initialize/Shutdown
Result initialize(void* reserved = nullptr) noexcept;
Result shutdown() noexcept;

// Memory helpers
Result allocate(usize size, void** out_ptr) noexcept;
Result deallocate(void* ptr) noexcept;

// Tick utilities
Result get_tick_count(TickCount* out) noexcept;

// String utilities (safe copy)
Result copy_cstr_safe(const char* src, char* dst, usize dst_capacity, usize* out_written) noexcept;

// Handle registry (simple example)
Result handle_create(Handle* out_handle) noexcept;
Result handle_destroy(Handle h) noexcept;
Result handle_is_valid(Handle h, BOOL* out_valid) noexcept;

} // namespace core

// -----------------------------
// Inline implementations for header-only performance-critical functions
// (Non-trivial implementations should be placed in .cpp to avoid ODR issues)
// -----------------------------
namespace core {

inline Result initialize(void* /*reserved*/) noexcept {
    // Minimal initialization; platform-specific subsystems should be initialized in implementation.
    return results::Ok;
}

inline Result shutdown() noexcept {
    // Minimal shutdown; platform-specific cleanup in implementation.
    return results::Ok;
}

inline Result allocate(usize size, void** out_ptr) noexcept {
    if (out_ptr == nullptr) return results::InvalidArgument;
    if (size == 0) { *out_ptr = nullptr; return results::Ok; }
    // Use std::malloc in header-only fallback; implementations should override with platform allocator.
    void* p = std::malloc(size);
    if (p == nullptr) return results::OutOfMemory;
    *out_ptr = p;
    return results::Ok;
}

inline Result deallocate(void* ptr) noexcept {
    if (ptr == nullptr) return results::Ok;
    std::free(ptr);
    return results::Ok;
}

inline Result get_tick_count(TickCount* out) noexcept {
    if (out == nullptr) return results::InvalidArgument;
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    auto ms = duration_cast<milliseconds>(now).count();
    out->milliseconds = static_cast<u64>(ms);
    return results::Ok;
}

inline Result copy_cstr_safe(const char* src, char* dst, usize dst_capacity, usize* out_written) noexcept {
    if (src == nullptr || dst == nullptr) return results::InvalidArgument;
    usize len = std::strlen(src);
    if (dst_capacity == 0) return results::BufferTooSmall;
    usize to_copy = (len < (dst_capacity - 1)) ? len : (dst_capacity - 1);
    std::memcpy(dst, src, to_copy);
    dst[to_copy] = '\0';
    if (out_written) *out_written = to_copy;
    return (to_copy < len) ? results::BufferTooSmall : results::Ok;
}

// Simple handle registry (header-only, not thread-safe; production must be thread-safe)
struct HandleRegistry {
    static constexpr usize MaxHandles = 1 << 20; // 1M handles
    struct Entry { u32 index; u16 generation; BOOL used; };
    Entry* table;
    usize capacity;
    u32 next_index;

    HandleRegistry() noexcept : table(nullptr), capacity(0), next_index(1u) {}

    Result init(usize cap) noexcept {
        if (cap == 0 || cap > MaxHandles) return results::InvalidArgument;
        if (table) return results::Failed;
        table = static_cast<Entry*>(std::malloc(sizeof(Entry) * cap));
        if (!table) return results::OutOfMemory;
        capacity = cap;
        for (usize i = 0; i < capacity; ++i) {
            table[i].index = static_cast<u32>(i + 1);
            table[i].generation = 1;
            table[i].used = false;
        }
        next_index = 1u;
        return results::Ok;
    }

    Result destroy() noexcept {
        if (table) {
            std::free(table);
            table = nullptr;
            capacity = 0;
            next_index = 1u;
        }
        return results::Ok;
    }

    Result create(Handle* out) noexcept {
        if (!out) return results::InvalidArgument;
        if (!table) return results::Failed;
        // Linear probe for free slot (fast in practice if low fragmentation)
        for (usize i = 0; i < capacity; ++i) {
            u32 idx = (next_index + static_cast<u32>(i)) % static_cast<u32>(capacity);
            Entry& e = table[idx];
            if (!e.used) {
                e.used = true;
                e.generation = static_cast<u16>(e.generation + 1);
                *out = HandleTraits::make(e.index, e.generation);
                next_index = (idx + 1) % static_cast<u32>(capacity);
                return results::Ok;
            }
        }
        return results::Failed;
    }

    Result destroy_handle(Handle h) noexcept {
        if (!HandleTraits::is_valid(h)) return results::InvalidArgument;
        u32 idx = HandleTraits::index(h);
        if (idx == 0 || idx > capacity) return results::InvalidArgument;
        Entry& e = table[idx - 1];
        if (!e.used) return results::NotFound;
        e.used = false;
        e.generation = static_cast<u16>(e.generation + 1);
        return results::Ok;
    }

    Result is_valid(Handle h, BOOL* out_valid) noexcept {
        if (!out_valid) return results::InvalidArgument;
        if (!HandleTraits::is_valid(h)) { *out_valid = false; return results::Ok; }
        u32 idx = HandleTraits::index(h);
        if (idx == 0 || idx > capacity) { *out_valid = false; return results::Ok; }
        Entry& e = table[idx - 1];
        u16 gen = HandleTraits::generation(h);
        *out_valid = (e.used && e.generation == gen);
        return results::Ok;
    }
};

static HandleRegistry g_handle_registry;

inline Result handle_create(Handle* out_handle) noexcept {
    if (out_handle == nullptr) return results::InvalidArgument;
    if (g_handle_registry.table == nullptr) {
        // Lazy init with conservative capacity; production should initialize explicitly.
        VSO_RETURN_ON_ERROR(g_handle_registry.init(4096));
    }
    return g_handle_registry.create(out_handle);
}

inline Result handle_destroy(Handle h) noexcept {
    if (g_handle_registry.table == nullptr) return results::NotFound;
    return g_handle_registry.destroy_handle(h);
}

inline Result handle_is_valid(Handle h, BOOL* out_valid) noexcept {
    return g_handle_registry.is_valid(h, out_valid);
}

} // namespace core

#undef VSO_CHECK_PTR
#undef VSO_CHECK_RANGE
#undef VSO_RETURN_ON_ERROR

} // namespace vso
