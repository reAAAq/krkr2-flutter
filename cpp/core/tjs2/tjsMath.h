//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS Math class implementation
// TJS2 Math 类实现 —— 对应脚本层 Math 内建对象，
// 封装 sin/cos/sqrt/random 等数学函数，以原生类形式注册到全局命名空间
//---------------------------------------------------------------------------
#ifndef tjsMathH
#define tjsMathH

#include "tjsNative.h"

namespace TJS {

    //---------------------------------------------------------------------------
    // Native class binding for TJS2's built-in Math object
    // TJS2 内建 Math 对象的原生类绑定
    class tTJSNC_Math : public tTJSNativeClass {
    public:
        tTJSNC_Math();

    private:
        static tjs_uint32 ClassID; // unique class identifier / 类唯一标识符
    };
    //---------------------------------------------------------------------------
} // namespace TJS

#endif
