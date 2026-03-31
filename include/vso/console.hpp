// include/vso/console.hpp
#pragma once

#include "vso/types.hpp"

namespace vso::console {

// 単純な文字列出力（改行なし）
Result Write(const char* text);

// 改行付き出力
Result WriteLine(const char* text);

// 画面クリア
Result Clear();

// 前景色だけ変える（背景は将来でもOK）
Result SetTextColor(ConsoleColor color);

// 必要なら将来：カーソル位置、入力などもここに追加

} // namespace vso::console
