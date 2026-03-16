//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS Exception Class
// TJS2 Exception 类 —— 对应 TJS2 脚本层的 Exception 内建类，
// 封装为原生类（tTJSNativeClass）供脚本通过 new Exception(...) 实例化
//---------------------------------------------------------------------------
#ifndef tjsExceptionH
#define tjsExceptionH

#include "tjsNative.h"

namespace TJS {
    //---------------------------------------------------------------------------
    // tTJSNC_Exception - native class binding for TJS2's built-in Exception type
    // tTJSNC_Exception - TJS2 内建 Exception 类型的原生类绑定
    //---------------------------------------------------------------------------
    class tTJSNC_Exception : public tTJSNativeClass {
    public:
        tTJSNC_Exception();

    private:
        static tjs_uint32 ClassID; // unique class identifier / 类唯一标识符
    };
    //---------------------------------------------------------------------------
} // namespace TJS

#endif
