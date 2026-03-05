#pragma once

#include <core/utils.h>

namespace Task
{
    enum class TaskState
    {
        RUNNING,
        READY,
        BLOCKED,
        SLEEPING,
        DEAD
    };

    constexpr int MAX_PRIORITY = 31;

    struct Task
    {
        uint64_t id;
        TaskState state;
        int priority, timeSlice;
        uint64_t context, kernelStack, stackSize;
        Task *next, *prev;

        void (*entry)(void*);
        void* arg;
    };

    Task* taskCreate(void (*entry)(void*), void* arg, int priority);
    void taskDestroy(Task* task);
}
