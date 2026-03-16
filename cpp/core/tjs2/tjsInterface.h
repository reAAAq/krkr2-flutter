//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// iTJSDispatch2 interface definition
// iTJSDispatch2 接口定义 —— TJS2 对象系统核心接口，所有可分派对象均继承自此
//---------------------------------------------------------------------------
#ifndef tjsInterfaceH
#define tjsInterfaceH

#include "tjsConfig.h"
#include "tjsErrorDefs.h"

namespace TJS {
//---------------------------------------------------------------------------

/*[*/
//---------------------------------------------------------------------------
// Member access / call flags  成员访问与调用标志位
//---------------------------------------------------------------------------
#define TJS_MEMBERENSURE 0x00000200  // create member if absent / 若成员不存在则创建
#define TJS_MEMBERMUSTEXIST                                                    \
    0x00000400  // member must exist (for Dictionary/Array) / 成员必须存在（用于字典/数组）
#define TJS_IGNOREPROP 0x00000800    // bypass property handler / 绕过属性 handler 直接访问
#define TJS_HIDDENMEMBER 0x00001000  // member is hidden / 隐藏成员（不在枚举中出现）
#define TJS_STATICMEMBER                                                       \
    0x00010000  // not registered to object (internal use) / 不注册到对象（内部使用）

#define TJS_ENUM_NO_VALUE                                                      \
    0x00100000  // skip value retrieval in EnumMembers / EnumMembers 时不读取成员值

// NativeInstance storage flags  本地实例存储标志
#define TJS_NIS_REGISTER    0x00000001  // set native pointer / 写入本地指针
#define TJS_NIS_GETINSTANCE 0x00000002  // get native pointer / 读取本地指针

// ClassInstanceInfo (CII) operation codes  类实例信息操作码
#define TJS_CII_ADD 0x00000001           // register member name / 注册成员名
// 'num' argument is ignored for ADD.
// ADD 操作时 num 参数被忽略。
#define TJS_CII_GET 0x00000000           // retrieve member name / 查询成员名

#define TJS_CII_SET_FINALIZE 0x00000002  // register "finalize" method / 注册 finalize 方法名
// Pass empty string to suppress finalize call.
// 传空字符串可禁止调用 finalize。
#define TJS_CII_SET_MISSING 0x00000003   // register "missing" method / 注册 missing 方法名
// Called when a member is not found; receives (get_or_set, name, *value).
// 成员不存在时调用；接收参数：get_or_set（false=读,true=写）、name、*value。
// Return true for found, false for not-found.
// 返回 true 表示已处理，false 表示未找到。
#define TJS_CII_SET_SUPRECLASS 0x00000004 // register super class instance / 注册超类实例
#define TJS_CII_GET_SUPRECLASS 0x00000005 // retrieve super class instance / 获取超类实例

// Object lock/unlock flags  对象加锁/解锁标志
#define TJS_OL_LOCK   0x00000001  // lock the object / 加锁
#define TJS_OL_UNLOCK 0x00000002  // unlock the object / 解锁

    //---------------------------------------------------------------------------
    // Operation flags — passed to iTJSDispatch2::Operation
    // 运算标志 —— 传入 iTJSDispatch2::Operation
    //---------------------------------------------------------------------------

#define TJS_OP_BAND 0x0001  // bitwise AND  (&=)  / 位与赋值
#define TJS_OP_BOR  0x0002  // bitwise OR   (|=)  / 位或赋值
#define TJS_OP_BXOR 0x0003  // bitwise XOR  (^=)  / 位异或赋值
#define TJS_OP_SUB  0x0004  // subtract     (-=)  / 减法赋值
#define TJS_OP_ADD  0x0005  // add          (+=)  / 加法赋值
#define TJS_OP_MOD  0x0006  // modulo       (%=)  / 取模赋值
#define TJS_OP_DIV  0x0007  // real divide  (/=)  / 实数除法赋值
#define TJS_OP_IDIV 0x0008  // integer div  (\=)  / 整数除法赋值
#define TJS_OP_MUL  0x0009  // multiply     (*=)  / 乘法赋值
#define TJS_OP_LOR  0x000a  // logical OR   (||=) / 逻辑或赋值
#define TJS_OP_LAND 0x000b  // logical AND  (&&=) / 逻辑与赋值
#define TJS_OP_SAR  0x000c  // arith right shift (>>=) / 算术右移赋值
#define TJS_OP_SAL  0x000d  // arith left  shift (<<=) / 算术左移赋值
#define TJS_OP_SR   0x000e  // logical right shift (>>>=) / 逻辑右移赋值
#define TJS_OP_INC  0x000f  // increment  (++) / 自增
#define TJS_OP_DEC  0x0010  // decrement  (--) / 自减

#define TJS_OP_MASK 0x001f  // mask for operation bits / 运算标志掩码

#define TJS_OP_MIN TJS_OP_BAND  // smallest op code / 最小运算码
#define TJS_OP_MAX TJS_OP_DEC   // largest  op code / 最大运算码

    /* SAR = Shift Arithmetic Right, SR = Shift (bitwise / unsigned) Right
     * SAR = 算术右移（保留符号位），SR = 逻辑右移（补零） */

    //---------------------------------------------------------------------------
    // iTJSDispatch
    //---------------------------------------------------------------------------
    /*
            iTJSDispatch interface
    */
    class tTJSVariant;

    class tTJSVariantClosure;

    class tTJSVariantString;

    class iTJSNativeInstance;

    class iTJSDispatch2 {
        /*
                methods, that have "ByNum" at the end of the name,
           have "num" parameter that enables the function to call a
           member with number directly. following two have the same
           effect: FuncCall(nullptr, L"123", nullptr, 0, nullptr,
           nullptr); FuncCallByNum(nullptr, 123, nullptr, 0, nullptr,
           nullptr);
        */

    public:
        virtual tjs_uint AddRef() = 0;

        virtual tjs_uint Release() = 0;

    public:
        virtual tjs_error FuncCall( // function invocation
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            tTJSVariant *result, // result
            tjs_int numparams, // number of parameters
            tTJSVariant **param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error FuncCallByNum( // function invocation by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            tTJSVariant *result, // result
            tjs_int numparams, // number of parameters
            tTJSVariant **param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error PropGet( // property get
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            tTJSVariant *result, // result
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error PropGetByNum( // property get by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            tTJSVariant *result, // result
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error PropSet( // property set
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            const tTJSVariant *param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error PropSetByNum( // property set by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            const tTJSVariant *param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error GetCount( // get member count
            tjs_int *result, // variable that receives the result
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error GetCountByNum( // get member count by index number
            tjs_int *result, // variable that receives the result
            tjs_int num, // by index number
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error PropSetByVS( // property set by tTJSVariantString, for
                                       // internal use
            tjs_uint32 flag, // calling flag
            tTJSVariantString *membername, // member name ( nullptr
                                           // for a default member )
            const tTJSVariant *param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error EnumMembers( // enumerate members
            tjs_uint32 flag, // enumeration flag
            tTJSVariantClosure *callback, // callback function interface (
                                          // called on each member )
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error DeleteMember( // delete member
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error DeleteMemberByNum( // delete member by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error Invalidate( // invalidation
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error InvalidateByNum( // invalidation by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error IsValid( // get validation, returns TJS_S_TRUE (valid)
                                   // or TJS_S_FALSE (invalid)
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error
        IsValidByNum( // get validation by index number, returns
                      // TJS_S_TRUE (valid) or TJS_S_FALSE (invalid)
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error CreateNew( // create new object
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            iTJSDispatch2 **result, // result
            tjs_int numparams, // number of parameters
            tTJSVariant **param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error CreateNewByNum( // create new object by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            iTJSDispatch2 **result, // result
            tjs_int numparams, // number of parameters
            tTJSVariant **param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error Reserved1() = 0;

        virtual tjs_error IsInstanceOf( // class instance matching returns
                                        // TJS_S_FALSE or TJS_S_TRUE
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            const tjs_char *classname, // class name to inquire
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error
        IsInstanceOfByNum( // class instance matching by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            const tjs_char *classname, // class name to inquire
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error Operation( // operation with member
            tjs_uint32 flag, // calling flag
            const tjs_char *membername, // member name ( nullptr for a
                                        // default member )
            tjs_uint32 *hint, // hint for the member name (in/out)
            tTJSVariant *result, // result ( can be nullptr )
            const tTJSVariant *param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error
        OperationByNum( // operation with member by index number
            tjs_uint32 flag, // calling flag
            tjs_int num, // index number
            tTJSVariant *result, // result ( can be nullptr )
            const tTJSVariant *param, // parameters
            iTJSDispatch2 *objthis // object as "this"
            ) = 0;

        virtual tjs_error NativeInstanceSupport( // support for native instance
            tjs_uint32 flag, // calling flag
            tjs_int32 classid, // native class ID
            iTJSNativeInstance **pointer // object pointer
            ) = 0;

        virtual tjs_error
        ClassInstanceInfo( // support for class instance information
            tjs_uint32 flag, // calling flag
            tjs_uint num, // index number
            tTJSVariant *value // the name
            ) = 0;

        virtual tjs_error Reserved2() = 0;

        virtual tjs_error Reserved3() = 0;
    };

    //---------------------------------------------------------------------------
    class iTJSNativeInstance {
    public:
        virtual tjs_error Construct(tjs_int numparams, tTJSVariant **param,
                                    iTJSDispatch2 *tjs_obj) = 0;

        // TJS constructor
        virtual void Invalidate() = 0;

        // called before destruction
        virtual void Destruct() = 0;
        // must destruct itself
    };
    /*]*/
    //---------------------------------------------------------------------------

} // namespace TJS

using namespace TJS;
#endif
