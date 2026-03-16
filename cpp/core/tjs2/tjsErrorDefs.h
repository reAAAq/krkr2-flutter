//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// return values as tjs_error
// tjs_error 返回值定义 —— TJS2 函数调用约定的错误码，类似 HRESULT
// 负值为失败，0 = TJS_S_OK，正值（1/2）为成功变体
//---------------------------------------------------------------------------
#pragma once

#include "tjsTypes.h"
namespace TJS {

// #define TJS_STRICT_ERROR_CODE_CHECK
// 定义此宏可启用严格错误码检查（调试用）。
// Defining this enables strict error code checking, for debugging.

//---------------------------------------------------------------------------
// return values as tjs_error / tjs_error 错误码定义
//---------------------------------------------------------------------------
#define TJS_E_MEMBERNOTFOUND (-1001) // member not found / 成员不存在
#define TJS_E_NOTIMPL        (-1002) // not implemented / 未实现
#define TJS_E_INVALIDPARAM   (-1003) // invalid parameter / 参数无效
#define TJS_E_BADPARAMCOUNT  (-1004) // wrong number of parameters / 参数数量错误
#define TJS_E_INVALIDTYPE    (-1005) // invalid type / 类型无效
#define TJS_E_INVALIDOBJECT  (-1006) // invalid or released object / 对象无效或已释放
#define TJS_E_ACCESSDENYED   (-1007) // access denied / 访问被拒绝
#define TJS_E_NATIVECLASSCRASH (-1008) // native class internal error / 原生类内部错误

#define TJS_S_TRUE  (1) // success: condition is true / 成功：条件为真
#define TJS_S_FALSE (2) // success: condition is false / 成功：条件为假

#define TJS_S_OK  (0)  // success / 成功
#define TJS_E_FAIL (-1) // generic failure / 通用失败

#define TJS_S_MAX (2)
    // maximum possible number of success status.
    // 最大成功状态值；超过此值在严格模式下视为失败。
    // numbers over this may be regarded as a failure in strict-checking mode.

#ifdef TJS_STRICT_ERROR_CODE_CHECK
    static inline bool TJS_FAILED(tjs_error hr) {
        if(hr < 0)
            return true;
        if(hr > TJS_S_MAX)
            return true;
        return false;
    }
#else
#define TJS_FAILED(x) ((x) < 0)
#endif
#define TJS_SUCCEEDED(x) (!TJS_FAILED(x))

    inline bool TJSIsObjectValid(tjs_error hr) {
        // checks object validity by returning value of
        // iTJSDispatch2::IsValid

        if(hr == TJS_S_TRUE)
            return true; // mostly expected value for valid object
        if(hr == TJS_E_NOTIMPL)
            return true; // also valid for object which does not
                         // implement IsValid

        return false; // otherwise the object is not valid
    }

} // namespace TJS
