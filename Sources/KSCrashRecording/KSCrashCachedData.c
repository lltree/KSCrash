//
//  KSCrashCachedData.c
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

#include "KSCrashCachedData.h"

// #define KSLogger_LocalLevel TRACE
#include <errno.h>
#include <mach/mach.h>
#include <memory.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "KSLogger.h"

#define SWAP_POINTERS(A, B) \
    {                       \
        void *temp = A;     \
        A = B;              \
        B = temp;           \
    }

static int g_pollingIntervalInSeconds;
static pthread_t g_cacheThread;
static KSThread *g_allMachThreads;
static KSThread *g_allPThreads;
static const char **g_allThreadNames;
static const char **g_allQueueNames;
static int g_allThreadsCount;
static _Atomic(int) g_semaphoreCount;
static bool g_searchQueueNames = false;
static bool g_hasThreadStarted = false;
/*
 已经崩溃了，减少开销
 为了减少内核态与用户态上下文切换，设计了在写报告时查询的缓存
 updateThreadList 是一个函数，用于更新当前进程中的线程列表信息。
 它通过调用 Mach 和 POSIX API 来获取活动线程的 Mach 线程句柄、POSIX 线程句柄（pthread_t），
 以及线程的名称和队列名称等信息，并将这些信息存储在全局变量中以供后续使用。
 */
static void updateThreadList(void)
{
    // 获取当前任务的 Mach task 句柄
    const task_t thisTask = mach_task_self();

    // 保存之前的线程数量，便于后续释放资源
    int oldThreadsCount = g_allThreadsCount;

    // 临时存储线程相关数据
    KSThread *allMachThreads = NULL;  // Mach 线程数组
    KSThread *allPThreads = NULL;     // POSIX 线程数组
    static const char **allThreadNames;  // 线程名称数组
    static const char **allQueueNames;   // 线程队列名称数组

    // 定义线程计数变量和线程列表存储
    mach_msg_type_number_t allThreadsCount;
    thread_act_array_t threads;
    kern_return_t kr;

    // 获取当前任务的所有线程
    if ((kr = task_threads(thisTask, &threads, &allThreadsCount)) != KERN_SUCCESS) {
        KSLOG_ERROR("task_threads: %s", mach_error_string(kr));
        return; // 如果失败，记录日志并退出
    }

    // 为线程数据分配内存
    allMachThreads = calloc(allThreadsCount, sizeof(*allMachThreads));
    allPThreads = calloc(allThreadsCount, sizeof(*allPThreads));
    allThreadNames = calloc(allThreadsCount, sizeof(*allThreadNames));
    allQueueNames = calloc(allThreadsCount, sizeof(*allQueueNames));

    // 遍历所有线程，提取相关信息
    for (mach_msg_type_number_t i = 0; i < allThreadsCount; i++) {
        char buffer[1000]; // 临时存储线程名称或队列名称
        thread_t thread = threads[i];
        pthread_t pthread = pthread_from_mach_thread_np(thread); // 获取 POSIX 线程

        // 保存 Mach 线程和 POSIX 线程句柄
        allMachThreads[i] = (KSThread)thread;
        allPThreads[i] = (KSThread)pthread;

        // 如果 POSIX 线程句柄有效，尝试获取线程名称
        if (pthread != 0 && pthread_getname_np(pthread, buffer, sizeof(buffer)) == 0 && buffer[0] != 0) {
            allThreadNames[i] = strdup(buffer); // 拷贝线程名称
        }

        // 如果启用了队列名称搜索，获取线程队列名称
        if (g_searchQueueNames && ksthread_getQueueName((KSThread)thread, buffer, sizeof(buffer)) && buffer[0] != 0) {
            allQueueNames[i] = strdup(buffer); // 拷贝队列名称
        }
    }

    // 更新全局线程数据，释放旧数据
    g_allThreadsCount = g_allThreadsCount < (int)allThreadsCount ? g_allThreadsCount : (int)allThreadsCount;
    SWAP_POINTERS(g_allMachThreads, allMachThreads);
    SWAP_POINTERS(g_allPThreads, allPThreads);
    SWAP_POINTERS(g_allThreadNames, allThreadNames);
    SWAP_POINTERS(g_allQueueNames, allQueueNames);
    g_allThreadsCount = (int)allThreadsCount;

    // 释放分配的临时内存
    if (allMachThreads != NULL) {
        free(allMachThreads);
    }
    if (allPThreads != NULL) {
        free(allPThreads);
    }
    if (allThreadNames != NULL) {
        for (int i = 0; i < oldThreadsCount; i++) {
            const char *name = allThreadNames[i];
            if (name != NULL) {
                free((void *)name);
            }
        }
        free(allThreadNames);
    }
    if (allQueueNames != NULL) {
        for (int i = 0; i < oldThreadsCount; i++) {
            const char *name = allQueueNames[i];
            if (name != NULL) {
                free((void *)name);
            }
        }
        free(allQueueNames);
    }

    // 释放 Mach 线程句柄数组
    for (mach_msg_type_number_t i = 0; i < allThreadsCount; i++) {
        mach_port_deallocate(thisTask, threads[i]);
    }

    // 释放线程数组内存
    vm_deallocate(thisTask, (vm_address_t)threads, sizeof(thread_t) * allThreadsCount);
}


static void *monitorCachedData(__unused void *const userData)
{
    static int quickPollCount = 4;
    usleep(1);
    for (;;) {
        if (g_semaphoreCount <= 0) {
            updateThreadList();
        }
        //默认1秒醒一次，60秒问询一次
        unsigned pollintInterval = (unsigned)g_pollingIntervalInSeconds;
        if (quickPollCount > 0) {
            // Lots can happen in the first few seconds of operation.
            quickPollCount--;
            pollintInterval = 1;
        }
        sleep(pollintInterval);
    }
    return NULL;
}

void ksccd_init(int pollingIntervalInSeconds)
{
    // 如果线程已经启动，则直接返回，避免重复初始化。
    if (g_hasThreadStarted == true) {
        return;
    }

    // 标记线程已启动
    g_hasThreadStarted = true;

    // 设置数据轮询的时间间隔（秒）
    g_pollingIntervalInSeconds = pollingIntervalInSeconds;

    // 初始化线程属性
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // 将线程设置为分离状态，线程完成后会自动释放资源。
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // 创建线程，运行函数 monitorCachedData。
    // 参数 "KSCrash Cached Data Monitor" 是传递给线程的上下文数据。
    int error = pthread_create(
        &g_cacheThread,          // 用于存储线程标识符的变量
        &attr,                   // 线程属性
        &monitorCachedData,      // 线程入口函数
        "KSCrash Cached Data Monitor" // 传递给线程的参数
    );

    // 如果线程创建失败，记录错误日志。
    if (error != 0) {
        KSLOG_ERROR("pthread_create_suspended_np: %s", strerror(error));
    }

    // 销毁线程属性对象，释放相关资源。
    pthread_attr_destroy(&attr);
}


void ksccd_freeze(void)
{
    if (g_semaphoreCount++ <= 0) {
        // Sleep just in case the cached data thread is in the middle of an update.
        usleep(1);
    }
}

void ksccd_unfreeze(void)
{
    if (--g_semaphoreCount < 0) {
        // Handle extra calls to unfreeze somewhat gracefully.
        g_semaphoreCount++;
    }
}

void ksccd_setSearchQueueNames(bool searchQueueNames) {
    g_searchQueueNames = searchQueueNames;
}

KSThread *ksccd_getAllThreads(int *threadCount)
{
    if (threadCount != NULL) {
        *threadCount = g_allThreadsCount;
    }
    return g_allMachThreads;
}

const char *ksccd_getThreadName(KSThread thread)
{
    if (g_allThreadNames != NULL) {
        for (int i = 0; i < g_allThreadsCount; i++) {
            if (g_allMachThreads[i] == thread) {
                return g_allThreadNames[i];
            }
        }
    }
    return NULL;
}

const char *ksccd_getQueueName(KSThread thread)
{
    if (g_allQueueNames != NULL) {
        for (int i = 0; i < g_allThreadsCount; i++) {
            if (g_allMachThreads[i] == thread) {
                return g_allQueueNames[i];
            }
        }
    }
    return NULL;
}
