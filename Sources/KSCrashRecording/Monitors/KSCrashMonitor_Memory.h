//
//  KSCrashMonitor_Memory.h
//
//  Created by Alexander Cohen on 2024-05-20.
//
//  Copyright (c) 2024 Alexander Cohen. All rights reserved.
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

/* Monitor memory and records data for OOMs.
 */

#ifndef KSCrashMonitor_Memory_h
#define KSCrashMonitor_Memory_h

#include "KSCrashAppTransitionState.h"
#include "KSCrashMonitor.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t KSCrash_Memory_Version_1_0;
extern const uint8_t KSCrash_Memory_CurrentVersion;

/** Non-Fatal report level where we don't report at all */
extern const uint8_t KSCrash_Memory_NonFatalReportLevelNone;

/**
 * 结构体 `KSCrash_Memory` 描述了应用程序在运行期间的内存使用状态。
 */
typedef struct KSCrash_Memory {
    /**
     * 魔数标记，用于验证结构体的合法性。
     */
    int32_t magic;

    /**
     * 结构体的当前版本，用于兼容性检查。
     */
    int8_t version;

    /**
     * 时间戳，单位为微秒，记录采集内存数据的时间点。
     */
    int64_t timestamp;

    /**
     * 应用程序已使用的内存量，单位为字节。
     */
    uint64_t footprint;

    /**
     * 应用程序剩余可用的内存量，单位为字节。
     */
    uint64_t remaining;

    /**
     * 内存占用的峰值（high water mark），等于 `footprint + remaining`。
     */
    uint64_t limit;

    /**
     * 当前的内存压力，使用枚举 `KSCrashAppMemoryPressure` 表示。
     */
    uint8_t pressure;

    /**
     * 当前的内存级别，使用枚举 `KSCrashAppMemoryLevel` 表示。
     * （如：正常、警告、紧急等级别）
     */
    uint8_t level;

    /**
     * 应用程序的状态转换，记录了内存相关的状态变化信息。
     */
    KSCrashAppTransitionState state;

    /**
     * 如果为 `true`，表示该进程发生了某种致命异常或事件。
     */
    bool fatal;

} KSCrash_Memory;


/** Access the Monitor API.
 */
KSCrashMonitorAPI *kscm_memory_getAPI(void);

/** Initialize the memory monitor.
 *
 * @param dataPath The data path of the KSCrash system.
 */
void ksmemory_initialize(const char *dataPath);

/** Returns true if the previous session was terminated due to memory.
 *
 * @param userPerceptible Set to true if the termination was visible
 * to the user or if they might have perceived it in any way (ie: app was active, or
 * during some sort of transition from background to active). Can be NULL.
 */
bool ksmemory_previous_session_was_terminated_due_to_memory(bool *userPerceptible);

/** Sets the minimum level at which to report non-fatals.
 *
 * @param level Minimum level at which we report non-fatals.
 *
 * @notes Default to no reporting. Use _KSCrash_Memory_NonFatalReportLevelNone_
 * to turn this feature off. Use any value in `KSCrashAppMemoryState` as a level.
 */
void ksmemory_set_nonfatal_report_level(uint8_t level);

/** Returns the minimum level at which memory non-fatals are reported.
 */
uint8_t ksmemory_get_nonfatal_report_level(void);

/** Enables or disables sending reports for memory terminations.
 *  Default to true.
 *
 * @param enabled if true, reports will be sent.
 */
void ksmemory_set_fatal_reports_enabled(bool enabled);

/** Returns true if fatal reports are enabled.
 */
bool ksmemory_get_fatal_reports_enabled(void);

/** Notifies memory monitoring logic that there was an unhandled fatal signal.
 *  E.g. SIGTERM is not considered as a crash by-default but ignoring this signal
 *  causes false-positive OOM reports.
 */
void ksmemory_notifyUnhandledFatalSignal(void);

#ifdef __cplusplus
}
#endif

#endif  // KSCrashMonitor_Memory_h
