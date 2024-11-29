//
//  KSStackCursor_Backtrace.c
//
//  Copyright (c) 2016 Karl Stenerud. All rights reserved.
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

#include "KSStackCursor_Backtrace.h"

#include "KSCPU.h"

// #define KSLogger_LocalLevel TRACE
#include "KSLogger.h"
/*
 advanceCursor 的主要任务是：
 检查是否还有可用的堆栈帧。
 如果有，则从当前深度获取下一个堆栈地址，更新堆栈条目，并推进游标的深度。
 */
static bool advanceCursor(KSStackCursor *cursor)
{
    // 从游标获取回溯上下文
    KSStackCursor_Backtrace_Context *context = (KSStackCursor_Backtrace_Context *)cursor->context;
    // 计算回溯的结束深度，考虑跳过的栈帧
    int endDepth = context->backtraceLength - context->skippedEntries;

    // 判断当前深度是否小于结束深度，确保还有栈帧
    if (cursor->state.currentDepth < endDepth) {
        // 根据当前深度和跳过的栈帧计算当前栈帧的索引
        int currentIndex = cursor->state.currentDepth + context->skippedEntries;
        uintptr_t nextAddress = context->backtrace[currentIndex];
        
        // 检查地址的有效性，避免无效地址
        if (nextAddress > 1) {
            // 规范化指令指针地址
            cursor->stackEntry.address = kscpu_normaliseInstructionPointer(nextAddress);
            // 增加当前深度
            cursor->state.currentDepth++;
            return true;
        }
    }
    
    // 如果没有更多栈帧，返回 false
    return false;
}

/*
 cursor
 要初始化的堆栈游标指针，表示一个遍历堆栈的游标结构。

 backtrace
 指向回溯信息的指针数组，每个元素是一个栈帧的地址。

 backtraceLength
 回溯数组的长度，表示栈帧数量。

 skipEntries
 指定在回溯开始时跳过的栈帧数量（用于忽略一些不需要的帧，如函数本身）。
 */
void kssc_initWithBacktrace(KSStackCursor *cursor, const uintptr_t *backtrace, int backtraceLength, int skipEntries)
{
    // 初始化堆栈游标结构，指定 kssc_resetCursor 和 advanceCursor 函数值
    kssc_initCursor(cursor, kssc_resetCursor, advanceCursor);

    // 获取游标的上下文，用于存储回溯相关数据
    KSStackCursor_Backtrace_Context *context = (KSStackCursor_Backtrace_Context *)cursor->context;

    // 设置上下文数据
    context->skippedEntries = skipEntries;        // 指定在回溯中需要跳过的栈帧数量
    context->backtraceLength = backtraceLength;   // 设置回溯数组的长度（即栈帧数量）
    context->backtrace = backtrace;               // 保存回溯数组的指针
}

