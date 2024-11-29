#import "KSCrashAppMemoryTracker.h"

#import "KSCrashAppMemory+Private.h"
#import "KSSystemCapabilities.h"

#import <mach/mach.h>
#import <mach/task.h>
#import <os/lock.h>

#if KSCRASH_HAS_UIAPPLICATION
#import <UIKit/UIKit.h>
#endif

/**
 The memory tracker takes care of centralizing the knowledge around memory.
 It does the following:

 1- Wraps memory pressure. This is more useful than `didReceiveMemoryWarning`
 as it vends different levels of pressure caused by the app as well as the rest of the OS.

 2- Vends a memory level. This is pretty novel. It vends levels of where the app is wihtin
 the memory limit.

 Some useful info.

 Memory Pressure is mostly useful when the app is in the background.
 It helps understand how much `pressure` is on the app due to external concerns. Using
 this data, we can make informed decisions around the reasons the app might have been
 terminated.

 Memory Level is useful in the foreground as well as background. It indicates where the app is
 within its memory limit. That limit being calculated by the addition of `remaining` and
 `footprint`. Using this data, we can also make informed decisions around foreground and background
 memory terminations, aka. OOMs.

 See: https://github.com/naftaly/Footprint
 */

// I'm not protecting this.
// It should be set once at start if you want to change it for tests.
static KSCrashAppMemoryProvider gMemoryProvider = nil;
FOUNDATION_EXPORT void __KSCrashAppMemorySetProvider(KSCrashAppMemoryProvider provider)
{
    gMemoryProvider = [provider copy];
}

@interface KSCrashAppMemoryTracker () {
    dispatch_queue_t _heartbeatQueue;
    dispatch_source_t _pressureSource;
    dispatch_source_t _limitSource;

    os_unfair_lock _lock;
    uint64_t _footprint;
    KSCrashAppMemoryState _pressure;
    KSCrashAppMemoryState _level;
}
@end

@implementation KSCrashAppMemoryTracker

- (instancetype)init
{
    if (self = [super init]) {
        _lock = OS_UNFAIR_LOCK_INIT;
        
        _heartbeatQueue = dispatch_queue_create_with_target("com.kscrash.memory.heartbeat", DISPATCH_QUEUE_SERIAL,
                                                            dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0));
        _level = KSCrashAppMemoryStateNormal;
        _pressure = KSCrashAppMemoryStateNormal;
    }
    return self;
}

- (void)dealloc
{
    [self stop];
}

- (void)start {
    // 停止旧的监控源（如果存在的话）
    if (_pressureSource || _limitSource) {
        [self stop];
    }

    // 内存压力监控
    uintptr_t mask = DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL;
    /*
     dispatch_source_create 是一个用于创建新的 Dispatch Source 的函数，它是 Grand Central Dispatch（GCD）框架的一部分，允许你创建一个源（source）来监听特定的系统事件或条件，并在事件发生时异步执行指定的代码块。
     */
    // 创建内存压力的 Dispatch Source源
    _pressureSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0, mask, dispatch_get_main_queue());
    
    __weak __typeof(self) weakMe = self; // 使用弱引用避免循环引用
    
    // 设置内存压力变化事件的回调
    dispatch_source_set_event_handler(_pressureSource, ^{
        [weakMe _memoryPressureChanged:YES]; // 内存压力发生变化时触发
    });
    dispatch_activate(_pressureSource); // 激活内存压力监控源

    // 内存水平（level）的监控
    _limitSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _heartbeatQueue); // 创建定时器 Source
    dispatch_source_set_event_handler(_limitSource, ^{
        [weakMe _heartbeat:YES]; // 定时触发心跳事件，检查内存限制
    });
    dispatch_source_set_timer(_limitSource,
                              dispatch_time(DISPATCH_TIME_NOW, 0), // 开始时间
                              NSEC_PER_SEC,                        // 每秒触发一次
                              NSEC_PER_SEC / 10);                  // 容错时间为 0.1 秒
    dispatch_activate(_limitSource); // 激活定时器

#if KSCRASH_HAS_UIAPPLICATION
    // 如果有 UIApplication，则监听应用完成启动的通知
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(_appDidFinishLaunching) // 应用完成启动后的回调
                                                 name:UIApplicationDidFinishLaunchingNotification
                                               object:nil];
#endif
    // 处理当前的应用内存变化
    [self _handleMemoryChange:[self currentAppMemory] type:KSCrashAppMemoryTrackerChangeTypeNone];
}


#if KSCRASH_HAS_UIAPPLICATION
- (void)_appDidFinishLaunching
{
    [self _handleMemoryChange:[self currentAppMemory] type:KSCrashAppMemoryTrackerChangeTypeNone];
}
#endif

- (void)stop
{
    if (_pressureSource) {
        dispatch_source_cancel(_pressureSource);
        _pressureSource = nil;
    }

    if (_limitSource) {
        dispatch_source_cancel(_limitSource);
        _limitSource = nil;
    }
}
#pragma mark - 获取AppMemory信息
static KSCrashAppMemory *_Nullable _ProvideCrashAppMemory(KSCrashAppMemoryState pressure)
{
    // 创建一个 `task_vm_info_data_t` 类型的结构体，用于存储任务的虚拟内存信息
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    // 使用 `task_info` 获取当前任务的虚拟内存信息
    kern_return_t err = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    if (err != KERN_SUCCESS) {
        // 如果获取失败，则返回 nil
        return nil;
    }

#if TARGET_OS_SIMULATOR
    // 模拟器环境下，`info.limit_bytes_remaining` 始终为 0。
    // 为了模拟内存限制，这里假定一个虚拟的内存上限（3GB）。
    uint64_t limit = 3000000000;  // 设置虚拟内存上限为 3GB
    uint64_t remaining = limit < info.phys_footprint ? 0 : limit - info.phys_footprint;
#elif KSCRASH_HOST_MAC
    // macOS 环境下，内存使用的限制方式与其他系统不同。
    // 为了简化处理，这里设置一个较大的虚拟内存上限（128GB）。
    uint64_t limit = 137438953472;  // 设置虚拟内存上限为 128GB
    uint64_t remaining = limit < info.phys_footprint ? 0 : limit - info.phys_footprint;
#else
    // 在其他设备上，直接使用 `info.limit_bytes_remaining` 表示剩余内存。
    uint64_t remaining = info.limit_bytes_remaining;
#endif

    // 返回一个初始化的 `KSCrashAppMemory` 实例，包含以下信息：
    // - 当前内存占用量（`info.phys_footprint`）
    // - 剩余可用内存（`remaining`）
    // - 内存压力状态（`pressure`）
    return [[KSCrashAppMemory alloc] initWithFootprint:info.phys_footprint remaining:remaining pressure:pressure];
}


- (nullable KSCrashAppMemory *)currentAppMemory
{
    return gMemoryProvider ? gMemoryProvider() : _ProvideCrashAppMemory(_pressure);
}

- (void)_handleMemoryChange:(KSCrashAppMemory *)memory type:(KSCrashAppMemoryTrackerChangeType)changes
{
    [self.delegate appMemoryTracker:self memory:memory changed:changes];
}

// in case of unsigned values
// ie: MAX(x,y) - MIN(x,y)
#define _KSABS_DIFF(x, y) x > y ? x - y : y - x

- (void)_heartbeat:(BOOL)sendObservers
{
    // This handles the memory limit.
    //定时去检查内存使用信息
    KSCrashAppMemory *memory = [self currentAppMemory];

    KSCrashAppMemoryState newLevel = memory.level;
    uint64_t newFootprint = memory.footprint;

    KSCrashAppMemoryState oldLevel;
    BOOL footprintChanged = NO;
    {
        os_unfair_lock_lock(&_lock);

        oldLevel = _level;
        _level = newLevel;

        // the amount footprint needs to change for any footprint notifs.
        const uint64_t kKSCrashFootprintMinChange = 1 << 20;  // 1 MiB

        // For the footprint, we don't need very granular changes,
        // changing a few bytes here or there won't mke a difference,
        // we're looking for anything larger.
        if (_KSABS_DIFF(newFootprint, _footprint) > kKSCrashFootprintMinChange) {
            _footprint = newFootprint;
            footprintChanged = YES;
        }

        os_unfair_lock_unlock(&_lock);
    }

    KSCrashAppMemoryTrackerChangeType changes =
        (newLevel != oldLevel) ? KSCrashAppMemoryTrackerChangeTypeLevel : KSCrashAppMemoryTrackerChangeTypeNone;

    if (footprintChanged) {
        changes |= KSCrashAppMemoryTrackerChangeTypeFootprint;
    }

    if (changes != KSCrashAppMemoryTrackerChangeTypeNone) {
        [self _handleMemoryChange:memory type:changes];
    }

    if (newLevel != oldLevel && sendObservers) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:KSCrashAppMemoryLevelChangedNotification
                                                                object:self
                                                              userInfo:@{
                                                                  KSCrashAppMemoryNewValueKey : @(newLevel),
                                                                  KSCrashAppMemoryOldValueKey : @(oldLevel)
                                                              }];
        });
#if TARGET_OS_SIMULATOR

        // On the simulator, if we're at a terminal level
        // let's fake an OOM by sending a SIGKILL signal
        //
        // NOTE: Some teams might want to do this in prod.
        // For example, we could send a SIGTERM so the system
        // catches a stack trace.
        static BOOL sIsRunningInTests;
        static BOOL sSimulatorMemoryKillEnabled;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            NSDictionary<NSString *, NSString *> *env = NSProcessInfo.processInfo.environment;
            sIsRunningInTests = env[@"XCTestSessionIdentifier"] != nil;
            sSimulatorMemoryKillEnabled = [env[@"KSCRASH_SIM_MEMORY_TERMINATION_ENABLED"] boolValue];
        });
        if (sSimulatorMemoryKillEnabled && !sIsRunningInTests && newLevel == KSCrashAppMemoryStateTerminal) {
            kill(getpid(), SIGKILL);
            _exit(0);
        }
#endif
    }
}

- (void)_memoryPressureChanged:(BOOL)sendObservers
{
    // This handles system based memory pressure.
    KSCrashAppMemoryState newPressure = KSCrashAppMemoryStateNormal;
    dispatch_source_memorypressure_flags_t flags = dispatch_source_get_data(_pressureSource);
    switch (flags) {
        case DISPATCH_MEMORYPRESSURE_NORMAL:
            newPressure = KSCrashAppMemoryStateNormal;
            break;

        case DISPATCH_MEMORYPRESSURE_WARN:
            newPressure = KSCrashAppMemoryStateWarn;
            break;

        case DISPATCH_MEMORYPRESSURE_CRITICAL:
            newPressure = KSCrashAppMemoryStateCritical;
            break;
    }

    KSCrashAppMemoryState oldPressure;
    {
        os_unfair_lock_lock(&_lock);
        oldPressure = _pressure;
        _pressure = newPressure;
        os_unfair_lock_unlock(&_lock);
    }

    if (oldPressure != newPressure && sendObservers) {
        [self _handleMemoryChange:[self currentAppMemory] type:KSCrashAppMemoryTrackerChangeTypePressure];
        [[NSNotificationCenter defaultCenter] postNotificationName:KSCrashAppMemoryPressureChangedNotification
                                                            object:self
                                                          userInfo:@{
                                                              KSCrashAppMemoryNewValueKey : @(newPressure),
                                                              KSCrashAppMemoryOldValueKey : @(oldPressure)
                                                          }];
    }
}

- (KSCrashAppMemoryState)pressure
{
    KSCrashAppMemoryState state;
    {
        os_unfair_lock_lock(&_lock);
        state = _pressure;
        os_unfair_lock_unlock(&_lock);
    }
    return state;
}

- (KSCrashAppMemoryState)level
{
    KSCrashAppMemoryState state;
    {
        os_unfair_lock_lock(&_lock);
        state = _level;
        os_unfair_lock_unlock(&_lock);
    }
    return state;
}

@end
