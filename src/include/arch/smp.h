#pragma once

#include <core/utils.h>

namespace SMP
{
    struct CPUFeatures
    {
        bool hasSSE, hasSSE2, hasSSE3, hasSSE4_1, hasSSE4_2, hasAVX;
    };

    void init();
    void detectCPUFeatures();
    uint32_t getCpuCount();
    uint32_t getLapicID();
    uint64_t getKernelStackTop(uint32_t cpuID);
    uint64_t getLapicBase();
    CPUFeatures getCPUFeatures();

    constexpr size_t MAX_CPUS = 256, SMP_STACK_SIZE = 8192;
    constexpr uintptr_t LAPIC_BASE = 0xFEE00000;
}
