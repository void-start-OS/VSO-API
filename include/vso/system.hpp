// include/vso/system.hpp
#pragma once

#include "vso/types.hpp"

namespace vso::system {

// OS を再起動する（戻らない想定）
[[noreturn]] void Reboot();

// 将来用：システム情報
struct SystemInfo {
    u64 totalMemoryBytes;
    u64 usedMemoryBytes;
    u32 cpuCount;
};

Result GetSystemInfo(SystemInfo& outInfo);

} // namespace vso::system
