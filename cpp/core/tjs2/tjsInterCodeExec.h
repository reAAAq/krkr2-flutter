//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Intermediate Code Execution
// 中间代码执行 —— VM 寄存器数组栈分配器，用于 ExecuteCode 的局部寄存器帧管理
//---------------------------------------------------------------------------
#pragma once

namespace TJS {

    // Trigger a deferred compaction of the variant array stack.
    // 触发变体数组栈的延迟紧缩（下次分配时执行）。
    extern void TJSVariantArrayStackCompact();

    // Trigger an immediate compaction of the variant array stack.
    // 立即执行变体数组栈紧缩，归还所有空闲内存。
    extern void TJSVariantArrayStackCompactNow();

    // Set the thread-local (or global) variant array stack instance.
    // 设置当前线程使用的变体数组栈实例。
    extern void TJSSetGlobalVariantArrayStack(class tTJSVariantArrayStack *stack);

    // Pool allocator for tTJSVariant register frames.
    // Each VM function call allocates a fixed-size register array from this pool
    // instead of calling new/delete per call, significantly reducing allocation overhead.
    // tTJSVariant 寄存器帧的池化分配器。
    // 每次 VM 函数调用从此池获取定长寄存器数组，避免逐次 new/delete，大幅降低分配开销。
    class tTJSVariantArrayStack {

        struct tVariantArray {
            tTJSVariant *Array;    // pointer to the allocated variant block / 变体块起始指针
            tjs_int Using;         // number of variants currently in use / 当前使用的变体数
            tjs_int Allocated;     // total allocated capacity / 已分配容量
        };

        tVariantArray *Arrays;             // array-of-arrays pool / 数组池
        tjs_int NumArraysAllocated;        // pool capacity / 池总容量
        tjs_int NumArraysUsing;            // pool usage / 池当前使用量
        tVariantArray *Current;            // current active sub-array / 当前活跃子数组
        tjs_int CompactVariantArrayMagic;  // compaction trigger counter / 紧缩触发计数器
        tjs_int OperationDisabledCount;    // disable compaction when > 0 / 大于0时禁用紧缩

        void IncreaseVariantArray(tjs_int num);

        void DecreaseVariantArray();

        void InternalCompact();

    public:
        tTJSVariantArrayStack();

        ~tTJSVariantArrayStack();

        // Allocate a register frame of 'num' tTJSVariant slots.
        // 从池中分配 num 个 tTJSVariant 槽位组成的寄存器帧。
        tTJSVariant *Allocate(tjs_int num);

        // Return a register frame back to the pool.
        // 将寄存器帧归还给池。
        void Deallocate(tjs_int num, tTJSVariant *ptr);

        // Request a deferred compaction.
        // 请求延迟紧缩。
        void Compact() { InternalCompact(); }
    };
    //---------------------------------------------------------------------------
} // namespace TJS
