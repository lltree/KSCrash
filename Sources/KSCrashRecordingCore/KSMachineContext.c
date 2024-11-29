//
//  KSMachineContext.c
//
//  Created by Karl Stenerud on 2016-12-02.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "KSMachineContext.h"

#include <mach/mach.h>

#if __has_include(<sys/_types/_ucontext64.h>)
#include <sys/_types/_ucontext64.h>
#endif

#include "KSCPU.h"
#include "KSCPU_Apple.h"
#include "KSMachineContext_Apple.h"
#include "KSStackCursor_MachineContext.h"
#include "KSSystemCapabilities.h"

// #define KSLogger_LocalLevel TRACE
#include "KSLogger.h"

#ifdef __arm64__
#if !(KSCRASH_HOST_MAC)
#define _KSCRASH_CONTEXT_64
#endif
#endif

#ifdef _KSCRASH_CONTEXT_64
#define UC_MCONTEXT uc_mcontext64
typedef ucontext64_t SignalUserContext;
#undef _KSCRASH_CONTEXT_64
#else
#define UC_MCONTEXT uc_mcontext
typedef ucontext_t SignalUserContext;
#endif

static KSThread g_reservedThreads[10];
static int g_reservedThreadsMaxIndex = sizeof(g_reservedThreads) / sizeof(g_reservedThreads[0]) - 1;
static int g_reservedThreadsCount = 0;

static inline bool isStackOverflow(const KSMachineContext *const context)
{
    KSStackCursor stackCursor;
    kssc_initWithMachineContext(&stackCursor, KSSC_STACK_OVERFLOW_THRESHOLD, context);
    while (stackCursor.advanceCursor(&stackCursor)) {
    }
    return stackCursor.state.hasGivenUp;
}

static inline bool getThreadList(KSMachineContext *context)
{
    // 获取当前任务（当前进程）的任务标识符
    const task_t thisTask = mach_task_self();

    // 打印日志，说明正在获取线程列表
    KSLOG_DEBUG("Getting thread list");

    kern_return_t kr;  // 存储函数返回的错误码
    thread_act_array_t threads;  // 存储线程列表的数组
    mach_msg_type_number_t actualThreadCount;  // 存储获取到的线程数

    // 获取当前进程的线程列表，task_threads 返回的是当前进程的所有线程
    if ((kr = task_threads(thisTask, &threads, &actualThreadCount)) != KERN_SUCCESS) {
        // 如果调用失败，输出错误日志并返回 false
        KSLOG_ERROR("task_threads: %s", mach_error_string(kr));
        return false;
    }

    // 打印日志，显示成功获取的线程数
    KSLOG_TRACE("Got %d threads", context->threadCount);

    int threadCount = (int)actualThreadCount;  // 将线程数转换为整型
    int maxThreadCount = sizeof(context->allThreads) / sizeof(context->allThreads[0]);  // 计算最大线程数

    // 如果线程数超过了最大限制，则做特殊处理
    if (threadCount > maxThreadCount) {
        // 输出错误日志，说明线程数超过最大值
        KSLOG_ERROR("Thread count %d is higher than maximum of %d", threadCount, maxThreadCount);

        // 如果崩溃线程超出了最大线程数的限制，将其移动到数组末尾
        for (mach_msg_type_number_t idx = maxThreadCount; idx < threadCount; ++idx) {
            if (threads[idx] == context->thisThread) {
                // 如果当前线程是崩溃线程，放到线程数组的最后一个位置
                threads[maxThreadCount - 1] = threads[idx];
                break;
            }
        }
        threadCount = maxThreadCount;  // 设置实际线程数为最大线程数
    }

    // 将获取到的线程列表复制到 context->allThreads 中
    for (int i = 0; i < threadCount; i++) {
        context->allThreads[i] = threads[i];
    }
    // 更新 context 中的线程数
    context->threadCount = threadCount;

    // 释放线程数组所占的内存
    for (mach_msg_type_number_t i = 0; i < actualThreadCount; i++) {
        mach_port_deallocate(thisTask, threads[i]);
    }
    vm_deallocate(thisTask, (vm_address_t)threads, sizeof(thread_t) * actualThreadCount);

    // 返回成功
    return true;
}


int ksmc_contextSize(void) { return sizeof(KSMachineContext); }

KSThread ksmc_getThreadFromContext(const KSMachineContext *const context) { return context->thisThread; }

bool ksmc_getContextForThread(KSThread thread, KSMachineContext *destinationContext, bool isCrashedContext)
{
    // 记录日志，打印线程信息、目标上下文指针地址和是否为崩溃上下文
    KSLOG_DEBUG("Fill thread 0x%x context into %p. is crashed = %d", thread, destinationContext, isCrashedContext);

    // 将目标上下文结构体清零，确保没有残留数据
    memset(destinationContext, 0, sizeof(*destinationContext));

    // 初始化上下文的基本信息
    destinationContext->thisThread = (thread_t)thread;  // 设置线程标识符
    destinationContext->isCurrentThread = thread == ksthread_self();  // 判断是否为当前线程
    destinationContext->isCrashedContext = isCrashedContext;  // 设置是否为崩溃上下文
    destinationContext->isSignalContext = false;  // 初始化为非信号上下文

    // 如果上下文支持获取 CPU 状态，则获取并填充相关信息
    //设置崩溃线程时的 CPU状态 __ss __es 栈状态
    if (ksmc_canHaveCPUState(destinationContext)) {
        kscpu_getState(destinationContext);  // 获取线程的 CPU 寄存器状态
    }

    // 如果当前上下文是崩溃上下文，处理相关特殊信息
    if (ksmc_isCrashedContext(destinationContext)) {
        destinationContext->isStackOverflow = isStackOverflow(destinationContext);  // 检查是否发生堆栈溢出
        getThreadList(destinationContext);  // 获取与上下文相关的线程列表
    }

    // 记录上下文获取完成的日志
    KSLOG_TRACE("Context retrieved.");

    // 返回 true 表示成功
    return true;
}


bool ksmc_getContextForSignal(void *signalUserContext, KSMachineContext *destinationContext)
{
    KSLOG_DEBUG("Get context from signal user context and put into %p.", destinationContext);
    _STRUCT_MCONTEXT *sourceContext = ((SignalUserContext *)signalUserContext)->UC_MCONTEXT;
    memcpy(&destinationContext->machineContext, sourceContext, sizeof(destinationContext->machineContext));
    destinationContext->thisThread = (thread_t)ksthread_self();
    destinationContext->isCrashedContext = true;
    destinationContext->isSignalContext = true;
    destinationContext->isStackOverflow = isStackOverflow(destinationContext);
    getThreadList(destinationContext);
    KSLOG_TRACE("Context retrieved.");
    return true;
}

void ksmc_addReservedThread(KSThread thread)
{
    int nextIndex = g_reservedThreadsCount;
    if (nextIndex > g_reservedThreadsMaxIndex) {
        KSLOG_ERROR("Too many reserved threads (%d). Max is %d", nextIndex, g_reservedThreadsMaxIndex);
        return;
    }
    g_reservedThreads[g_reservedThreadsCount++] = thread;
}

#if KSCRASH_HAS_THREADS_API
static inline bool isThreadInList(thread_t thread, KSThread *list, int listCount)
{
    for (int i = 0; i < listCount; i++) {
        if (list[i] == (KSThread)thread) {
            return true;
        }
    }
    return false;
}
#endif

void ksmc_suspendEnvironment(__unused thread_act_array_t *suspendedThreads,
                             __unused mach_msg_type_number_t *numSuspendedThreads)
{
#if KSCRASH_HAS_THREADS_API
    KSLOG_DEBUG("Suspending environment.");
    kern_return_t kr;
    const task_t thisTask = mach_task_self();//示当前任务（即当前应用进程）。
    const thread_t thisThread = (thread_t)ksthread_self();//表示当前正在运行的线程。

    //调用 task_threads 获取当前任务中的所有线程，并将线程列表存储到 suspendedThreads 中，同时将线程数存储到 numSuspendedThreads。
    if ((kr = task_threads(thisTask, suspendedThreads, numSuspendedThreads)) != KERN_SUCCESS) {
        KSLOG_ERROR("task_threads: %s", mach_error_string(kr));
        return;
    }

    //遍历 task_threads 获取的线程列表。
    for (mach_msg_type_number_t i = 0; i < *numSuspendedThreads; i++) {
        thread_t thread = (*suspendedThreads)[i];
        
        if (thread != thisThread && !isThreadInList(thread, g_reservedThreads, g_reservedThreadsCount)) {
            //如果线程需要暂停，调用 thread_suspend 函数。
            if ((kr = thread_suspend(thread)) != KERN_SUCCESS) {
                // Record the error and keep going.
                KSLOG_ERROR("thread_suspend (%08x): %s", thread, mach_error_string(kr));
            }
        }
    }

    KSLOG_DEBUG("Suspend complete.");
#endif
}

void ksmc_resumeEnvironment(__unused thread_act_array_t threads, __unused mach_msg_type_number_t numThreads)
{
#if KSCRASH_HAS_THREADS_API
    KSLOG_DEBUG("Resuming environment.");
    kern_return_t kr;
    const task_t thisTask = mach_task_self();
    const thread_t thisThread = (thread_t)ksthread_self();

    if (threads == NULL || numThreads == 0) {
        KSLOG_ERROR("we should call ksmc_suspendEnvironment() first");
        return;
    }

    for (mach_msg_type_number_t i = 0; i < numThreads; i++) {
        thread_t thread = threads[i];
        if (thread != thisThread && !isThreadInList(thread, g_reservedThreads, g_reservedThreadsCount)) {
            if ((kr = thread_resume(thread)) != KERN_SUCCESS) {
                // Record the error and keep going.
                KSLOG_ERROR("thread_resume (%08x): %s", thread, mach_error_string(kr));
            }
        }
    }

    for (mach_msg_type_number_t i = 0; i < numThreads; i++) {
        mach_port_deallocate(thisTask, threads[i]);
    }
    vm_deallocate(thisTask, (vm_address_t)threads, sizeof(thread_t) * numThreads);

    KSLOG_DEBUG("Resume complete.");
#endif
}

int ksmc_getThreadCount(const KSMachineContext *const context) { return context->threadCount; }

KSThread ksmc_getThreadAtIndex(const KSMachineContext *const context, int index) { return context->allThreads[index]; }

int ksmc_indexOfThread(const KSMachineContext *const context, KSThread thread)
{
    KSLOG_TRACE("check thread vs %d threads", context->threadCount);
    for (int i = 0; i < (int)context->threadCount; i++) {
        KSLOG_TRACE("%d: %x vs %x", i, thread, context->allThreads[i]);
        if (context->allThreads[i] == thread) {
            return i;
        }
    }
    return -1;
}

bool ksmc_isCrashedContext(const KSMachineContext *const context) { return context->isCrashedContext; }

static inline bool isContextForCurrentThread(const KSMachineContext *const context) { return context->isCurrentThread; }

static inline bool isSignalContext(const KSMachineContext *const context) { return context->isSignalContext; }

bool ksmc_canHaveCPUState(const KSMachineContext *const context)
{
    return !isContextForCurrentThread(context) || isSignalContext(context);
}

bool ksmc_hasValidExceptionRegisters(const KSMachineContext *const context)
{
    return ksmc_canHaveCPUState(context) && ksmc_isCrashedContext(context);
}
