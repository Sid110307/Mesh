#pragma once

#include <arch/common/atomic.h>
#include <arch/common/spinlock.h>
#include <core/utils.h>

class SMP
{
public:
    static void init();
    static uint32_t getCpuCount();
    static uint32_t getCpuID();
    static uint32_t getLapicID();

    static constexpr size_t MAX_CPUS = 256, SMP_STACK_SIZE = 8192;
    static constexpr uintptr_t LAPIC_BASE = 0xFEE00000;
    static Atomic apReadyCount;
    static uint32_t cpuCount;

private:
    static void waitForAPs();
    static Spinlock smpLock;
};
