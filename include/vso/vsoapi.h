#pragma once

//
//  Void Start OS - Official API Header
//  vsoapi.h
//
//  このヘッダを include するだけで全 API が使える。
//  Win32API の windows.h と同じ思想。
//  C++17 以降を前提。
//  

#include "types.hpp"
#include "console.hpp"
#include "console_format.hpp"
#include "memory.hpp"
#include "system.hpp"
#include "time.hpp"

//
//  API バージョン管理（将来の互換性のため）
//
#define VSOAPI_VERSION_MAJOR 1
#define VSOAPI_VERSION_MINOR 0
#define VSOAPI_VERSION_PATCH 0

namespace vso {

inline constexpr uint32_t GetApiVersion() {
    return (VSOAPI_VERSION_MAJOR << 16) |
           (VSOAPI_VERSION_MINOR << 8)  |
            VSOAPI_VERSION_PATCH;
}

} // namespace vso
