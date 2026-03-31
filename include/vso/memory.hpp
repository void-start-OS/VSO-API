// include/vso/memory.hpp
#pragma once

#include "vso/types.hpp"

namespace vso::memory {

// 生メモリ確保（OS の物理/仮想メモリサブシステムに委譲）
void* Alloc(usize size);

// 解放
void  Free(void* ptr);

// 安全版：Result + out 引数
Result Alloc(usize size, void** outPtr);

} // namespace vso::memory
