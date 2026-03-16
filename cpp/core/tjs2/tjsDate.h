//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Date class implementation
// TJS2 Date 类实现 —— 对应脚本层 Date 内建类，封装 POSIX time_t 时间戳
//---------------------------------------------------------------------------

#ifndef tjsDateH
#define tjsDateH

#include <time.h>
#include "tjsNative.h"

namespace TJS {
    //---------------------------------------------------------------------------
    // Native instance holding the actual timestamp / 持有实际时间戳的原生实例
    class tTJSNI_Date : public tTJSNativeInstance {
    public:
        tTJSNI_Date();

        time_t DateTime; // POSIX timestamp (seconds since epoch) / POSIX 时间戳（自 epoch 起的秒数）

    private:
    };

    //---------------------------------------------------------------------------
    // Native class factory for TJS2's built-in Date type
    // TJS2 内建 Date 类型的原生类工厂
    class tTJSNC_Date : public tTJSNativeClass {
    public:
        tTJSNC_Date();

        static tjs_uint32 ClassID; // unique class identifier / 类唯一标识符

    private:
        tTJSNativeInstance *CreateNativeInstance() override;
    };
    //---------------------------------------------------------------------------
} // namespace TJS

#endif
