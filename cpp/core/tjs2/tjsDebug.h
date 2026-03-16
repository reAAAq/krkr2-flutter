//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS Debugging support
// TJS2 调试支持 —— 栈追踪器（StackTracer）、断点触发与调试信息查询接口
//---------------------------------------------------------------------------
#ifndef tjsDebugH
#define tjsDebugH

#include "tjs.h"
#include "tjsString.h"

namespace TJS {

#ifdef _DEBUG
    // Key identifying a unique scope (class + function + file + code offset)
    // for the debug stack tracer. / 调试栈追踪器使用的作用域唯一标识键。
    struct ScopeKey {
        int ClassIndex; //!< class name index / クラス名インデックス / 类名索引
        int FuncIndex;  //!< function name index / 関数名インデックス / 函数名索引
        int FileIndex;  //!< file name index / ファイル名インデックス / 文件名索引
        int CodeOffset; //!< VM code offset / VM コードオフセット / VM 代码偏移量

        ScopeKey() :
            ClassIndex(-1), FuncIndex(-1), FileIndex(-1), CodeOffset(-1) {}
        ScopeKey(int classidx, int func, int file, int codeoffset) :
            ClassIndex(classidx), FuncIndex(func), FileIndex(file),
            CodeOffset(codeoffset) {}
        void Set(int classidx, int func, int file, int codeoffset) {
            ClassIndex = classidx;
            FuncIndex = func;
            FileIndex = file;
            CodeOffset = codeoffset;
        }

        bool operator==(const ScopeKey &rhs) const {
            return (ClassIndex == rhs.ClassIndex &&
                    FuncIndex == rhs.FuncIndex && FileIndex == rhs.FileIndex &&
                    CodeOffset == rhs.CodeOffset);
        }
        bool operator!=(const ScopeKey &rhs) const {
            return (ClassIndex != rhs.ClassIndex ||
                    FuncIndex != rhs.FuncIndex || FileIndex != rhs.FileIndex ||
                    CodeOffset != rhs.CodeOffset);
        }
        bool operator<(const ScopeKey &rhs) const {
            // クラス、関数名
            if(ClassIndex == rhs.ClassIndex) {
                if(FuncIndex == rhs.FuncIndex) {
                    if(FileIndex == rhs.FileIndex) {
                        return CodeOffset < rhs.CodeOffset;
                    } else {
                        return FileIndex < rhs.FileIndex;
                    }
                } else {
                    return FuncIndex < rhs.FuncIndex;
                }
            } else {
                return ClassIndex < rhs.ClassIndex;
            }
        }
    };
#endif // _DEBUG

//---------------------------------------------------------------------------
// ObjectHashMap : hash map to track object construction/destruction
//---------------------------------------------------------------------------
// object hash map flags
#define TJS_OHMF_EXIST 1 // The object is in object hash map
#define TJS_OHMF_INVALIDATED                                                   \
    2 // The object had been invalidated  // currently not used
#define TJS_OHMF_DELETING 4 // The object is now being deleted
#define TJS_OHMF_SET (~0)
#define TJS_OHMF_UNSET (0)

    //---------------------------------------------------------------------------
    class tTJSScriptBlock;

    struct tTJSObjectHashMapRecord;

    class tTJSObjectHashMap;

    extern tTJSObjectHashMap *TJSObjectHashMap;
    extern tTJSBinaryStream *TJSObjectHashMapLog;

    extern void TJSAddRefObjectHashMap();

    extern void TJSReleaseObjectHashMap();

    extern void TJSAddObjectHashRecord(void *object);

    extern void TJSRemoveObjectHashRecord(void *object);

    extern void TJSObjectHashSetType(void *object, const ttstr &type);

    extern void TJSSetObjectHashFlag(void *object, tjs_uint32 flags_to_change,
                                     tjs_uint32 bits);

    extern void TJSReportAllUnfreedObjects(iTJSConsoleOutput *output);

    extern bool TJSObjectHashAnyUnfreed();

    extern void TJSObjectHashMapSetLog(tTJSBinaryStream *stream);

    extern void TJSWriteAllUnfreedObjectsToLog();

    extern void TJSWarnIfObjectIsDeleting(iTJSConsoleOutput *output,
                                          void *object);

    extern void TJSReplayObjectHashMapLog();

    static inline bool TJSObjectHashMapEnabled() {
        return TJSObjectHashMap || TJSObjectHashMapLog;
    }

    extern inline bool TJSObjectTypeInfoEnabled() {
        return nullptr != TJSObjectHashMap;
    }

    extern inline bool TJSObjectFlagEnabled() {
        return nullptr != TJSObjectHashMap;
    }

    extern ttstr TJSGetObjectTypeInfo(void *object);

    extern tjs_uint32 TJSGetObjectHashCheckFlag(void *object);
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    // StackTracer : stack to trace function call trace
    //---------------------------------------------------------------------------
    class tTJSStackTracer;

    class tTJSInterCodeContext;

    extern tTJSStackTracer *TJSStackTracer;

    extern void TJSAddRefStackTracer();

    extern void TJSReleaseStackTracer();

    extern void TJSStackTracerPush(tTJSInterCodeContext *context, bool in_try);

    extern void TJSStackTracerSetCodePointer(const tjs_int32 *codebase,
                                             tjs_int32 *const *codeptr);

    extern void TJSStackTracerPop();

    extern ttstr TJSGetStackTraceString(tjs_int limit = 0,
                                        const tjs_char *delimiter = nullptr);

    static inline bool TJSStackTracerEnabled() {
        return nullptr != TJSStackTracer;
    }
    //---------------------------------------------------------------------------

} // namespace TJS

#endif
