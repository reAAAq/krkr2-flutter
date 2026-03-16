//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS Global String Map
// TJS2 全局字符串映射表 —— 对常量字符串进行去重（interning），使相同内容共享同一实例
// 减少内存占用，并使字符串相等比较可退化为指针比较
//---------------------------------------------------------------------------
#ifndef tjsGlobalStringMapH
#define tjsGlobalStringMapH

#include "tjsString.h"

namespace TJS {
    //---------------------------------------------------------------------------
    // tTJSGlobalStringMap - hash map to keep constant strings shared
    // tTJSGlobalStringMap - 哈希表，用于保持常量字符串的共享（字符串驻留）
    //---------------------------------------------------------------------------

    // Increment the reference count of the global string map.
    // 增加全局字符串映射表的引用计数（引擎初始化时调用）。
    extern void TJSAddRefGlobalStringMap();

    // Decrement the reference count; destroy the map when it reaches zero.
    // 减少引用计数；归零时销毁映射表（引擎销毁时调用）。
    extern void TJSReleaseGlobalStringMap();

    // Look up or insert a string into the global map and return the shared instance.
    // 在全局映射表中查找或插入字符串，返回共享实例（实现字符串驻留）。
    TJS_EXP_FUNC_DEF(ttstr, TJSMapGlobalStringMap, (const ttstr &string));
    //---------------------------------------------------------------------------
} // namespace TJS

#endif
