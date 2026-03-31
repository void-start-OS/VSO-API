// include/vso/types.hpp
#pragma once

#include <cstdint>

namespace vso {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using usize = std::size_t;

using DWORD = std::uint32_t;
using QWORD = std::uint64_t;
using BOOL  = bool;

enum class Result : u32 {
    Ok              = 0,
    Failed          = 1,
    InvalidArgument = 2,
    NotFound        = 3,
    OutOfMemory     = 4,
    NotImplemented  = 5,
};

enum class ConsoleColor : u8 {
    Default = 0,
    Black,
    Blue,
    Green,
    Cyan,
    Red,
    Magenta,
    Brown,
    LightGray,
    DarkGray,
    LightBlue,
    LightGreen,
    LightCyan,
    LightRed,
    LightMagenta,
    Yellow,
    White,
};

struct TickCount {
    u64 milliseconds;
};

} // namespace vso
