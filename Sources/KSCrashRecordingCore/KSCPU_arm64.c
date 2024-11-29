//
//  KSCPU_arm64_Apple.c
//
//  Created by Karl Stenerud on 2013-09-29.
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

#if defined(__arm64__)

#include <stdlib.h>

#include "KSCPU.h"
#include "KSCPU_Apple.h"
#include "KSMachineContext.h"
#include "KSMachineContext_Apple.h"

// #define KSLogger_LocalLevel TRACE
#include "KSLogger.h"

#define KSPACStrippingMask_ARM64e 0x0000000fffffffff

static const char *g_registerNames[] = { "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",
                                         "x9",  "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                                         "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26",
                                         "x27", "x28", "x29", "fp",  "lr",  "sp",  "pc",  "cpsr" };
static const int g_registerNamesCount = sizeof(g_registerNames) / sizeof(*g_registerNames);

static const char *g_exceptionRegisterNames[] = { "exception", "esr", "far" };
static const int g_exceptionRegisterNamesCount = sizeof(g_exceptionRegisterNames) / sizeof(*g_exceptionRegisterNames);

uintptr_t kscpu_framePointer(const KSMachineContext *const context) { return context->machineContext.__ss.__fp; }

uintptr_t kscpu_stackPointer(const KSMachineContext *const context) { return context->machineContext.__ss.__sp; }

uintptr_t kscpu_instructionAddress(const KSMachineContext *const context) { return context->machineContext.__ss.__pc; }

uintptr_t kscpu_linkRegister(const KSMachineContext *const context) { return context->machineContext.__ss.__lr; }

void kscpu_getState(KSMachineContext *context)
{
    // 获取当前线程的标识（thread_t），它用于在系统中查找和操作当前线程
    thread_t thread = context->thisThread;

    // 获取机器上下文（machineContext）的指针，它包含当前线程的寄存器状态
    STRUCT_MCONTEXT_L *const machineContext = &context->machineContext;

    //__ss 栈状态
    // 填充当前线程的常规状态（ARM_THREAD_STATE64），
    // 这个状态结构包含了诸如程序计数器、栈指针、通用寄存器等信息。
    // 将这些信息存储在 machineContext->__ss 位置。
    kscpu_i_fillState(thread, (thread_state_t)&machineContext->__ss, ARM_THREAD_STATE64, ARM_THREAD_STATE64_COUNT);

    //__es 额外数据段
    // 填充当前线程的异常状态（ARM_EXCEPTION_STATE64），
    // 该状态结构通常包括异常类型、异常的返回地址等。
    // 将这些信息存储在 machineContext->__es 位置。
    kscpu_i_fillState(thread, (thread_state_t)&machineContext->__es, ARM_EXCEPTION_STATE64,
                      ARM_EXCEPTION_STATE64_COUNT);
}

int kscpu_numRegisters(void) { return g_registerNamesCount; }

const char *kscpu_registerName(const int regNumber)
{
    if (regNumber < kscpu_numRegisters()) {
        return g_registerNames[regNumber];
    }
    return NULL;
}

uint64_t kscpu_registerValue(const KSMachineContext *const context, const int regNumber)
{
    if (regNumber <= 29) {
        return context->machineContext.__ss.__x[regNumber];
    }

    switch (regNumber) {
        case 30:
            return context->machineContext.__ss.__fp;
        case 31:
            return context->machineContext.__ss.__lr;
        case 32:
            return context->machineContext.__ss.__sp;
        case 33:
            return context->machineContext.__ss.__pc;
        case 34:
            return context->machineContext.__ss.__cpsr;
    }

    KSLOG_ERROR("Invalid register number: %d", regNumber);
    return 0;
}

int kscpu_numExceptionRegisters(void) { return g_exceptionRegisterNamesCount; }

const char *kscpu_exceptionRegisterName(const int regNumber)
{
    if (regNumber < kscpu_numExceptionRegisters()) {
        return g_exceptionRegisterNames[regNumber];
    }
    KSLOG_ERROR("Invalid register number: %d", regNumber);
    return NULL;
}

uint64_t kscpu_exceptionRegisterValue(const KSMachineContext *const context, const int regNumber)
{
    switch (regNumber) {
        case 0:
            return context->machineContext.__es.__exception;
        case 1:
            return context->machineContext.__es.__esr;
        case 2:
            return context->machineContext.__es.__far;
    }

    KSLOG_ERROR("Invalid register number: %d", regNumber);
    return 0;
}

uintptr_t kscpu_faultAddress(const KSMachineContext *const context) { return context->machineContext.__es.__far; }

int kscpu_stackGrowDirection(void) { return -1; }

uintptr_t kscpu_normaliseInstructionPointer(uintptr_t ip) { return ip & KSPACStrippingMask_ARM64e; }

#endif
