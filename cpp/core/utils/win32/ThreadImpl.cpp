//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Thread base class
//---------------------------------------------------------------------------
#define NOMINMAX

#include "tjsCommHead.h"

// #include <process.h>
#include <algorithm>

#include "ThreadIntf.h"
#include "ThreadImpl.h"
#include "MsgIntf.h"
#include "DebugIntf.h"

#if defined(CC_TARGET_OS_IPHONE) || defined(__aarch64__)
#else
// #define USING_THREADPOOL11
#endif

#ifdef USING_THREADPOOL11
#include "threadpool11/pool.hpp"
#endif

#include <thread>

//---------------------------------------------------------------------------
// tTVPThread : a wrapper class for thread
//---------------------------------------------------------------------------
tTVPThread::tTVPThread(bool suspended) {
    Terminated = false;
    Suspended = suspended;

    try {
        Handle = std::thread([this] { StartProc(this); });
        // Do NOT detach: we need Handle.joinable() == true so that
        // WaitFor() (join) works correctly. Detaching was a legacy
        // pattern that made it impossible to synchronize on thread
        // exit, leading to use-after-free on member mutexes.
    } catch(const std::system_error &) {
        // 捕获线程创建失败异常
        TVPThrowInternalError;
    }
}

//---------------------------------------------------------------------------
tTVPThread::~tTVPThread() {
    // Ensure the std::thread is not joinable when destroyed.
    // Subclass destructors SHOULD call Terminate() + WaitFor() first.
    // This is a safety net to avoid std::terminate().
    if(Handle.joinable()) {
        Terminated = true;
        _cond.notify_one(); // wake up if suspended
        Handle.join();
    }
}

//---------------------------------------------------------------------------
void *tTVPThread::StartProc(void *arg) {
    auto *_this = (tTVPThread *)arg;
    if(_this->Suspended) {
        std::unique_lock lk(_this->_mutex);
        _this->_cond.wait(lk);
    }
    _this->Execute();
    _this->Finished.store(true, std::memory_order_release);
    TVPOnThreadExited();
    return nullptr;
}

//---------------------------------------------------------------------------
void tTVPThread::WaitFor() {
    if(Handle.joinable()) {
        Handle.join();
    }
    // If the thread was somehow detached before (shouldn't happen with
    // the current code), spin-wait on the Finished flag as a safety net.
    while(!Finished.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

//---------------------------------------------------------------------------
tTVPThreadPriority tTVPThread::GetPriority() {
    // TODO: impl
    return ttpNormal;
}

//---------------------------------------------------------------------------
void tTVPThread::SetPriority(tTVPThreadPriority pri) {
    // TODO: impl
}

//---------------------------------------------------------------------------
// void tTVPThread::Suspend()
// {
// 	SuspendThread(Handle);
// }
//---------------------------------------------------------------------------
void tTVPThread::Resume() {
    Suspended = false;
    _cond.notify_one();
    // while((tjs_int32)ResumeThread(Handle) > 1) ;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPThreadEvent
//---------------------------------------------------------------------------
void tTVPThreadEvent::Set() {
    std::unique_lock lk(Mutex);
    Handle.notify_one();
}

//---------------------------------------------------------------------------
void tTVPThreadEvent::WaitFor(tjs_uint timeout) {
    // wait for event;
    // returns true if the event is set, otherwise (when timed out)
    // returns false.

    std::unique_lock lk(Mutex);
    if(timeout != 0) {
        Handle.wait_for(lk, std::chrono::milliseconds(timeout));
    } else {
        Handle.wait(lk);
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
tjs_int TVPDrawThreadNum = 1;

//---------------------------------------------------------------------------
static tjs_int GetProcesserNum() {
    static tjs_int processor_num = 0;
    if(!processor_num) {
        processor_num = std::thread::hardware_concurrency();
        tjs_char tmp[34];
        TVPAddLog(ttstr(TJS_W("Detected CPU core(s): ")) +
                  TJS_tTVInt_to_str(processor_num, tmp));
    }
    return processor_num;
}

tjs_int TVPGetProcessorNum() { return GetProcesserNum(); }

//---------------------------------------------------------------------------
tjs_int TVPGetThreadNum() {
    tjs_int threadNum = TVPDrawThreadNum ? TVPDrawThreadNum : GetProcesserNum();
    threadNum = std::min(threadNum, TVPMaxThreadNum);
    return threadNum;
}

//---------------------------------------------------------------------------
void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func) {
    if(numThreads == 1) {
        func(0);
        return;
    }
#if !defined(USING_THREADPOOL11)
#pragma omp parallel for schedule(static)
    for(int i = 0; i < numThreads; ++i)
        func(i);
#else
    static threadpool11::Pool pool;
    std::vector<std::future<void>> futures;
    for(int i = 0; i < numThreads; ++i) {
        futures.emplace_back(pool.postWork<void>(std::bind(func, i)));
    }
    for(auto &it : futures)
        it.get();
#endif
#if 0
    ThreadInfo *threadInfo;
    threadInfo = TVPThreadList[TVPThreadTaskCount++];
    threadInfo->lpStartAddress = func;
    threadInfo->lpParameter = param;
    InterlockedIncrement(&TVPRunningThreadCount);
    while (ResumeThread(threadInfo->thread) == 0)
      Sleep(0);
#endif
}
//---------------------------------------------------------------------------

std::vector<std::function<void()>> _OnThreadExitedEvents;

void TVPOnThreadExited() {
    for(const auto &ev : _OnThreadExitedEvents) {
        ev();
    }
}

void TVPAddOnThreadExitEvent(const std::function<void()> &ev) {
    _OnThreadExitedEvents.emplace_back(ev);
}
