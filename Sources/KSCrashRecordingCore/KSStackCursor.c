//
//  KSStackCursor.h
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

#include "KSStackCursor.h"

#include <stdlib.h>

#include "KSSymbolicator.h"

// #define KSLogger_LocalLevel TRACE
#include "KSLogger.h"

static bool g_advanceCursor(__unused KSStackCursor *cursor)
{
    KSLOG_WARN(
        "No stack cursor has been set. For C++, this means that hooking __cxa_throw() failed for some reason. Embedded "
        "frameworks can cause this: https://github.com/kstenerud/KSCrash/issues/205");
    return false;
}

void kssc_resetCursor(KSStackCursor *cursor)
{
    cursor->state.currentDepth = 0;
    cursor->state.hasGivenUp = false;
    cursor->stackEntry.address = 0;
    cursor->stackEntry.imageAddress = 0;
    cursor->stackEntry.imageName = NULL;
    cursor->stackEntry.symbolAddress = 0;
    cursor->stackEntry.symbolName = NULL;
}

void kssc_initCursor(KSStackCursor *cursor, void (*resetCursor)(KSStackCursor *),
                     bool (*advanceCursor)(KSStackCursor *))
{
    //设置符号化函数
    //将堆栈游标的符号化函数设置为 kssymbolicator_symbolicate，该函数负责将栈帧地址解析为符号信息（如方法名或函数名）。
    cursor->symbolicate = kssymbolicator_symbolicate;
    //如果调用者提供了 advanceCursor 函数指针，就使用它；否则使用默认的 g_advanceCursor。
    cursor->advanceCursor = advanceCursor != NULL ? advanceCursor : g_advanceCursor;
    cursor->resetCursor = resetCursor != NULL ? resetCursor : kssc_resetCursor;
    //调用游标的重置函数以初始化状态。这一步完成后，cursor 将处于有效状态，准备进行堆栈遍历。
    cursor->resetCursor(cursor);
}
