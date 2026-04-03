// src/vso/memory.cpp
// 実装: vso::memory
// - 高信頼・高性能な低レベルメモリ割当てラッパ
// - プラットフォームごとに最適なバックエンドを選択 (VirtualAlloc/mmap/malloc)
// - 割当情報はポインタ直前の小さなヘッダに格納して解放時に適切に処理
// - 境界チェック、オーバーフロー防止、ページ丸め、遅延初期化を実装
// - ヘッダ: include/vso/memory.hpp を前提
//
// 設計方針
// 1) API: void* Alloc(usize size)         -> size==0 の場合は nullptr を返す（呼び出し側の簡潔性）
//    void  Free(void* ptr)                 -> nullptr は無視
//    Result Alloc(usize size, void** out)  -> out==nullptr は InvalidArgument
// 2) 内部ヘッダは最小限かつアライン済み。ABI 安定性を意識して固定サイズ。
// 3) mmap/VirtualAlloc を使う場合はページ丸めして OS に直接要求する（大きな割当てで効率的）
// 4) 小さな割当ては malloc を使う（実装の単純さと互換性）
// 5) スレッド安全: グローバル初期化は std::call_once で行う
// 6) セキュリティ: 新規割当てはゼロ初期化しない（パフォーマンス優先）。必要なら呼び出し側で memset を行う。
//    （将来的にフラグでゼロ化を選べるよう拡張可能）
//

#include "vso/memory.hpp"
#include "vso/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <mutex>
#include <atomic>
#include <cassert>
#include <limits>
#include <type_traits>

#if defined(_WIN32) || defined(_WIN64)
#  define VSO_PLATFORM_WINDOWS 1
#  include <windows.h>
#else
#  define VSO_PLATFORM_POSIX 1
#  include <sys/mman.h>
#  include <unistd.h>
#  include <errno.h>
#endif

namespace vso::memory {

namespace {

// ヘッダ情報：割当て元を識別して適切に解放するためにポインタ直前に配置する。
// サイズはアライン済みで、ポインタの戻り値はヘッダの直後を指す。
struct alignas(alignof(std::max_align_t)) AllocHeader {
    // 固定マジックで簡易検査
    static constexpr u32 Magic = 0x564F534D; // 'VOSM'
    u32 magic;        // Magic
    u32 backend;      // 0=malloc, 1=mmap, 2=virtualalloc
    u64 size;         // ユーザ要求サイズ（バイト）
};

// backend 値
enum Backend : u32 {
    Backend_Malloc = 0,
    Backend_MMap   = 1,
    Backend_VAlloc = 2,
};

// ページサイズキャッシュ
static std::once_flag g_page_init_flag;
static usize g_page_size = 4096u;

inline void init_page_size() noexcept {
#if defined(VSO_PLATFORM_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_page_size = static_cast<usize>(si.dwPageSize ? si.dwPageSize : 4096u);
#else
    long ps = sysconf(_SC_PAGESIZE);
    g_page_size = static_cast<usize>((ps > 0) ? ps : 4096u);
#endif
}

// ヘッダサイズ（アライン済み）
constexpr usize header_size() noexcept {
    return static_cast<usize>( ( (sizeof(AllocHeader) + alignof(std::max_align_t) - 1) / alignof(std::max_align_t) ) * alignof(std::max_align_t) );
}

// 安全な加算チェック：a + b が overflow しないか
inline bool add_overflow(usize a, usize b, usize &out) noexcept {
    if (b > std::numeric_limits<usize>::max() - a) return true;
    out = a + b;
    return false;
}

// size をページ境界に丸める（上方向）
inline usize round_up_to_page(usize size) noexcept {
    std::call_once(g_page_init_flag, init_page_size);
    usize ps = g_page_size;
    if (ps == 0) ps = 4096u;
    usize rem = size % ps;
    if (rem == 0) return size;
    return size + (ps - rem);
}

// malloc バックエンド
inline void* backend_malloc_alloc(usize total_bytes) noexcept {
    // malloc の失敗は nullptr を返す
    return std::malloc(total_bytes);
}

inline void backend_malloc_free(void* base_ptr) noexcept {
    std::free(base_ptr);
}

#if defined(VSO_PLATFORM_POSIX)
// mmap バックエンド（POSIX）
inline void* backend_mmap_alloc(usize total_bytes) noexcept {
    // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
    void* p = mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    return p;
}

inline void backend_mmap_free(void* base_ptr, usize total_bytes) noexcept {
    if (base_ptr == nullptr) return;
    munmap(base_ptr, total_bytes);
}
#endif

#if defined(VSO_PLATFORM_WINDOWS)
// VirtualAlloc バックエンド（Windows）
inline void* backend_valloc_alloc(usize total_bytes) noexcept {
    // MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    void* p = VirtualAlloc(nullptr, total_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return p;
}

inline void backend_valloc_free(void* base_ptr, usize /*total_bytes*/) noexcept {
    if (base_ptr == nullptr) return;
    VirtualFree(base_ptr, 0, MEM_RELEASE);
}
#endif

// 実際の割当て処理（内部）
// 戻り値: ユーザポインタ（ヘッダ直後）または nullptr
// backend_prefer: -1 = 自動選択, 0=malloc, 1=mmap, 2=valloc
inline void* allocate_internal(usize size, int backend_prefer = -1) noexcept {
    if (size == 0) return nullptr;

    // ヘッダサイズを取得
    const usize hsize = header_size();

    // total = header + payload
    usize total;
    if (add_overflow(hsize, size, total)) return nullptr;

    // 自動選択ロジック:
    // - 大きな割当て（ページ単位で丸めた後のサイズがページ1つ以上かつ >= 16KB）なら OS レベルの割当てを試す
    // - 小さな割当ては malloc を使う
    std::call_once(g_page_init_flag, init_page_size);
    const usize page_rounded = round_up_to_page(total);

    bool try_mmap = false;
    bool try_valloc = false;
#if defined(VSO_PLATFORM_WINDOWS)
    if (backend_prefer == Backend_VAlloc) try_valloc = true;
    else if (backend_prefer == Backend_MMap) try_mmap = true; // unlikely on Windows
    else {
        // Windows: 大きめは VirtualAlloc
        if (page_rounded >= (16u * 1024u)) try_valloc = true;
    }
#else
    if (backend_prefer == Backend_MMap) try_mmap = true;
    else if (backend_prefer == Backend_Malloc) try_mmap = false;
    else {
        if (page_rounded >= (16u * 1024u)) try_mmap = true;
    }
#endif

    // 1) OS-level allocation (mmap/VirtualAlloc)
    if (try_mmap) {
#if defined(VSO_PLATFORM_POSIX)
        void* base = backend_mmap_alloc(page_rounded);
        if (base) {
            // ヘッダを先頭に書き込み、ユーザポインタを返す
            auto hdr = reinterpret_cast<AllocHeader*>(base);
            hdr->magic = AllocHeader::Magic;
            hdr->backend = Backend_MMap;
            hdr->size = size;
            // ユーザポインタはヘッダ直後（ヘッダはページ境界に収まる）
            void* user_ptr = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(base) + hsize);
            return user_ptr;
        }
        // mmap に失敗したらフォールバックして malloc を試す
#endif
    }

#if defined(VSO_PLATFORM_WINDOWS)
    if (try_valloc) {
        void* base = backend_valloc_alloc(page_rounded);
        if (base) {
            auto hdr = reinterpret_cast<AllocHeader*>(base);
            hdr->magic = AllocHeader::Magic;
            hdr->backend = Backend_VAlloc;
            hdr->size = size;
            void* user_ptr = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(base) + hsize);
            return user_ptr;
        }
        // VirtualAlloc に失敗したらフォールバックして malloc を試す
    }
#endif

    // 2) malloc ベース（ヘッダを含めた total を malloc）
    void* base_malloc = backend_malloc_alloc(total);
    if (!base_malloc) return nullptr;
    auto hdr = reinterpret_cast<AllocHeader*>(base_malloc);
    hdr->magic = AllocHeader::Magic;
    hdr->backend = Backend_Malloc;
    hdr->size = size;
    void* user_ptr = reinterpret_cast<void*>(reinterpret_cast<std::byte*>(base_malloc) + hsize);
    return user_ptr;
}

// 解放内部実装
inline void free_internal(void* user_ptr) noexcept {
    if (user_ptr == nullptr) return;

    const usize hsize = header_size();
    // ヘッダ位置を計算
    auto base = reinterpret_cast<std::byte*>(user_ptr) - static_cast<std::ptrdiff_t>(hsize);
    auto hdr = reinterpret_cast<AllocHeader*>(base);

    // マジックチェック（簡易）
    if (hdr->magic != AllocHeader::Magic) {
        // マジック不一致：ユーザが不正なポインタを渡した可能性がある。
        // 安全のため何もしない（または abort する選択肢もあるが、ここでは無害にする）。
        return;
    }

    const usize user_size = static_cast<usize>(hdr->size);

    // total を再計算
    usize total;
    if (add_overflow(hsize, user_size, total)) {
        // オーバーフローはあり得ないが、安全のため malloc free を試す
        if (hdr->backend == Backend_Malloc) {
            backend_malloc_free(base);
        }
        return;
    }

    switch (hdr->backend) {
    case Backend_Malloc:
        backend_malloc_free(base);
        break;
#if defined(VSO_PLATFORM_POSIX)
    case Backend_MMap:
        // mmap はページ丸めされたサイズで解放する必要がある
        {
            usize page_total = round_up_to_page(total);
            backend_mmap_free(base, page_total);
        }
        break;
#endif
#if defined(VSO_PLATFORM_WINDOWS)
    case Backend_VAlloc:
        backend_valloc_free(base, static_cast<usize>(0)); // VirtualFree ignores size when MEM_RELEASE
        break;
#endif
    default:
        // 不明な backend: 安全のため何もしない
        break;
    }
}

} // namespace (internal)

// Public API 実装

void* Alloc(usize size) {
    // size==0 は nullptr を返す（呼び出し側で扱いやすくする）
    if (size == 0) return nullptr;

    // サイズ上限チェック（極端に大きい要求は拒否）
    if (size > (static_cast<usize>(std::numeric_limits<usize>::max() / 2))) {
        return nullptr;
    }

    void* p = allocate_internal(size, -1);
    return p;
}

void Free(void* ptr) {
    if (ptr == nullptr) return;
    free_internal(ptr);
}

Result Alloc(usize size, void** outPtr) {
    if (outPtr == nullptr) return results::InvalidArgument;
    *outPtr = nullptr;

    if (size == 0) {
        // 仕様: size==0 は nullptr を返し成功とする
        *outPtr = nullptr;
        return results::Ok;
    }

    // サイズ上限チェック
    if (size > (static_cast<usize>(std::numeric_limits<usize>::max() / 2))) {
        return results::InvalidArgument;
    }

    void* p = allocate_internal(size, -1);
    if (!p) return results::OutOfMemory;
    *outPtr = p;
    return results::Ok;
}

} // namespace vso::memory
