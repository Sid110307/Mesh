#include <arch/x86_64/cpu.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <task/scheduler.h>

void push(Scheduler::RunQueue& queue, Task::Task* task)
{
    task->next = nullptr;

    if (!queue.head) queue.head = queue.tail = task;
    else
    {
        queue.tail->next = task;
        queue.tail = task;
    }
}

Task::Task* pop(Scheduler::RunQueue& queue)
{
    Task::Task* task = queue.head;
    if (!task) return nullptr;

    queue.head = task->next;
    if (!queue.head) queue.tail = nullptr;
    task->next = nullptr;

    return task;
}

void freeDead(Scheduler::Scheduler* scheduler, const Task::Task* current)
{
    Task::Task** head = &scheduler->head;
    while (*head)
    {
        Task::Task* task = *head;
        if (task == current)
        {
            head = &task->next;
            continue;
        }

        *head = task->next;
        Task::taskDestroy(task);
    }
}

void Scheduler::initCPU(Scheduler* scheduler, Task::Task* idleTask)
{
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->idleTask = idleTask;
    scheduler->currentTask = idleTask;
}

void Scheduler::addReady(Scheduler* scheduler, Task::Task* task)
{
    if (!task) return;

    const int p = task->priority;
    RunQueue& queue = scheduler->queues[p];

    push(queue, task);
    scheduler->bitmap |= 1u << p;
    task->state = Task::TaskState::READY;
}

Task::Task* Scheduler::pickNextTask(Scheduler* scheduler)
{
    if (!scheduler->bitmap) return scheduler->idleTask;

    const int p = 31 - __builtin_clz(scheduler->bitmap);
    RunQueue& queue = scheduler->queues[p];
    Task::Task* task = pop(queue);

    if (!queue.head) scheduler->bitmap &= ~(1u << p);
    return task ? task : scheduler->idleTask;
}

uint64_t Scheduler::onTimerIRQ(Scheduler* scheduler)
{
    CPU* cpu = CPUManager::getCurrentCPU();
    LAPIC::timerIrq();
    LAPIC::sendEOI();

    Task::Task* current = cpu->currentTask;
    freeDead(scheduler, current);
    if (!current) return 0;

    if (current != scheduler->idleTask && current->state == Task::TaskState::RUNNING)
    {
        current->timeSlice--;
        if (current->timeSlice > 0) return 0;

        current->timeSlice = 10;
        addReady(scheduler, current);
    }

    Task::Task* next = pickNextTask(scheduler);
    if (!next) next = scheduler->idleTask;
    if (next == current) return 0;

    cpu->currentTask = next;
    scheduler->currentTask = next;
    next->state = Task::TaskState::RUNNING;
    cpu->ticks++;
    cpu->preemptedTasks++;

    return next->context;
}

uint64_t Scheduler::onYieldIRQ(Scheduler* scheduler)
{
    CPU* cpu = CPUManager::getCurrentCPU();
    Task::Task* current = cpu->currentTask;
    freeDead(scheduler, current);

    if (!current) return 0;
    if (current != scheduler->idleTask && current->state == Task::TaskState::RUNNING) addReady(scheduler, current);

    Task::Task* next = pickNextTask(scheduler);
    if (!next) next = scheduler->idleTask;
    if (next == current) return 0;

    cpu->currentTask = next;
    scheduler->currentTask = next;
    next->state = Task::TaskState::RUNNING;

    return next->context;
}

extern "C" Task::Task* schedulerGetCurrentTask() { return CPUManager::getCurrentCPU()->currentTask; }

extern "C" uint64_t schedulerTimerIRQ(Interrupt::TimerFrame*)
{
    return Scheduler::onTimerIRQ(CPUManager::getCurrentCPU()->scheduler);
}

extern "C" uint64_t schedulerYieldIRQ(Interrupt::TimerFrame*)
{
    return Scheduler::onYieldIRQ(CPUManager::getCurrentCPU()->scheduler);
}
