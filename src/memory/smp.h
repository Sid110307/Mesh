#pragma once

#include <kernel/sync/atomic.h>
#include <kernel/sync/spinlock.h>
#include <kernel/core/utils.h>

struct ApBootInfo
{
    uint64_t stackTop;
    uint32_t cpuID;
};

struct CPUFeatures
{
    bool hasSSE, hasSSE2, hasSSE3, hasSSE4_1, hasSSE4_2, hasAVX;
};

class SMP
{
public:
    static void init();
    static void detectCPUFeatures();
    static uint32_t getCpuCount();
    static uint32_t getLapicID();
    static uint64_t getKernelStackTop(uint32_t cpuID);
    static uint64_t getLapicBase();

    static constexpr size_t MAX_CPUS = 256, SMP_STACK_SIZE = 8192;
    static constexpr uintptr_t LAPIC_BASE = 0xFEE00000;

    static Atomic apReadyCount;
    static CPUFeatures cpuFeatures;
    static uint32_t cpuCount;

private:
    static void waitForAPs();
    static Spinlock smpLock;
    static ApBootInfo apBoot[MAX_CPUS];
    static uint32_t apCount;
    static uint64_t lapicPhysBase, lapicVirtBase;
};
