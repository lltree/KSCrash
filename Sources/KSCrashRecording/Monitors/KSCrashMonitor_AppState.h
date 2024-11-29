//
//  KSCrashMonitor_AppState.h
//
//  Created by Karl Stenerud on 2012-02-05.
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

/* Manages persistent state information useful for crash reporting such as
 * number of sessions, session length, etc.
 */

#ifndef HDR_KSCrashMonitor_AppState_h
#define HDR_KSCrashMonitor_AppState_h

#include <stdbool.h>

#include "KSCrashMonitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // 保存的数据

    /**
     * 自上次崩溃以来，应用程序处于活跃状态的总时间（单位：秒）。
     */
    double activeDurationSinceLastCrash;

    /**
     * 自上次崩溃以来，应用程序处于后台状态的总时间（单位：秒）。
     */
    double backgroundDurationSinceLastCrash;

    /**
     * 自上次崩溃以来，应用程序启动的次数。
     */
    int launchesSinceLastCrash;

    /**
     * 自上次崩溃以来的会话数，会话包括应用启动和从挂起恢复的总次数。
     */
    int sessionsSinceLastCrash;

    /**
     * 自应用本次启动以来，应用程序处于活跃状态的总时间（单位：秒）。
     */
    double activeDurationSinceLaunch;

    /**
     * 自应用本次启动以来，应用程序处于后台状态的总时间（单位：秒）。
     */
    double backgroundDurationSinceLaunch;

    /**
     * 自应用本次启动以来的会话数，会话包括启动和从挂起恢复的总次数。
     */
    int sessionsSinceLaunch;

    /**
     * 如果为 `true`，表示应用程序在上次启动时发生了崩溃。
     */
    bool crashedLastLaunch;

    // 动态数据

    /**
     * 如果为 `true`，表示应用程序在本次启动时发生了崩溃。
     */
    bool crashedThisLaunch;

    /**
     * 记录应用状态最近一次发生改变（如从活跃切换为非活跃，或从后台切换到前台）的时间戳。
     */
    double appStateTransitionTime;

    /**
     * 如果为 `true`，表示应用程序当前处于活跃状态。
     */
    bool applicationIsActive;

    /**
     * 如果为 `true`，表示应用程序当前处于前台状态。
     */
    bool applicationIsInForeground;

} KSCrash_AppState;


/** Initialize the state monitor.
 *
 * @param stateFilePath Where to store on-disk representation of state.
 */
void kscrashstate_initialize(const char *stateFilePath);

/** Reset the crash state.
 */
bool kscrashstate_reset(void);

/** Notify the crash reporter of KSCrash being added to Objective-C runtime system.
 */
void kscrashstate_notifyObjCLoad(void);

/** Notify the crash reporter of the application active state.
 *
 * @param isActive true if the application is active, otherwise false.
 */
void kscrashstate_notifyAppActive(bool isActive);

/** Notify the crash reporter of the application foreground/background state.
 *
 * @param isInForeground true if the application is in the foreground, false if
 *                 it is in the background.
 */
void kscrashstate_notifyAppInForeground(bool isInForeground);

/** Notify the crash reporter that the application is terminating.
 */
void kscrashstate_notifyAppTerminate(void);

/** Notify the crash reporter that the application has crashed.
 */
void kscrashstate_notifyAppCrash(void);

/** Read-only access into the current state.
 */
const KSCrash_AppState *const kscrashstate_currentState(void);

/** Access the Monitor API.
 */
KSCrashMonitorAPI *kscm_appstate_getAPI(void);

#ifdef __cplusplus
}
#endif

#endif  // HDR_KSCrashMonitor_AppState_h
