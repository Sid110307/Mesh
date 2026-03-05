#pragma once

#include <arch/x86_64/smp.h>
#include <core/utils.h>
#include <task/scheduler.h>
#include <task/task.h>

struct CPU
{
    CPU* self;
    uint32_t id, lapicId;
    bool started;
    uint64_t kernelStack;
    Task::Task *currentTask, *idleTask;
    Scheduler::Scheduler* scheduler;
    uint64_t ticks, preemptedTasks;
};

extern CPU cpus[SMP::MAX_CPUS];

namespace CPUManager
{
    void initCPU(uint32_t cpuId, uint32_t lapicId);
    CPU* getCurrentCPU();
    uint32_t getCurrentCPUId();
}
