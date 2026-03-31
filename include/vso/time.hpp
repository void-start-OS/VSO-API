// include/vso/time.hpp
#pragma once

#include "vso/types.hpp"

namespace vso::time {

// 指定ミリ秒スリープ
Result Sleep(u32 milliseconds);

// 起動後経過時間（ミリ秒）
TickCount GetTickCount();

} // namespace vso::time

