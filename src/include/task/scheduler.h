#pragma once

#include <core/utils.h>
#include <memory/spinlock.h>
#include <task/task.h>

namespace Scheduler
{
    struct RunQueue
    {
        Task::Task *head = nullptr, *tail = nullptr;
    };

    struct Scheduler
    {
        RunQueue queues[Task::MAX_PRIORITY + 1];
        Spinlock lock;
        uint32_t bitmap = 0, cpuId = 0;
        Task::Task *currentTask = nullptr, *idleTask = nullptr, *deadHead = nullptr;
    };

    void initCPU(Scheduler* scheduler, Task::Task* idleTask);
    void addReady(Scheduler* scheduler, Task::Task* task);
    Task::Task* pickNextTask(Scheduler* scheduler);
    uint64_t onTimerIRQ(Scheduler* scheduler);
    uint64_t onYieldIRQ(Scheduler* scheduler);
}
