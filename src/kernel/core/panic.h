#pragma once

#include <kernel/core/utils.h>
#include <kernel/arch/isr.h>

namespace Panic
{
    void panic(const char* fmt, ...);
    void panicFrame(const InterruptFrame* frame, const char* fmt, ...);
    void printStackTrace(uint64_t basePointer = 0, int maxFrames = 32);
}
