#include <arch/x86_64/cpu.h>
#include <arch/x86_64/isr.h>
#include <memory/slab.h>
#include <task/scheduler.h>
#include <task/task.h>

extern "C" void taskTrampoline(Task::Task* task);
uint64_t nextTaskId = 1;

extern "C" void taskStart()
{
    extern Task::Task* schedulerGetCurrentTask();
    taskTrampoline(schedulerGetCurrentTask());
}

Task::Task* Task::taskCreate(void (*entry)(void*), void* arg, int priority)
{
    if (!entry) return nullptr;
    if (priority < 0) priority = 0;
    if (priority > MAX_PRIORITY) priority = MAX_PRIORITY;

    auto* t = static_cast<Task*>(SlabAllocator::alloc(sizeof(Task), alignof(Task)));
    if (!t) return nullptr;
    memset(t, 0, sizeof(Task));

    t->id = nextTaskId++;
    t->state = TaskState::READY;
    t->priority = priority;
    t->timeSlice = 10;
    t->entry = entry;
    t->arg = arg;
    t->stackSize = 16384;
    t->kernelStack = reinterpret_cast<uint64_t>(SlabAllocator::alloc(t->stackSize, 16));

    if (!t->kernelStack)
    {
        SlabAllocator::free(t);
        return nullptr;
    }

    const uint64_t sp = ((t->kernelStack + t->stackSize) & ~0xFULL) - sizeof(Interrupt::Frame);
    auto* frame = reinterpret_cast<Interrupt::Frame*>(sp);
    memset(frame, 0, sizeof(Interrupt::Frame));

    frame->rdi = reinterpret_cast<uint64_t>(t);
    frame->rip = reinterpret_cast<uint64_t>(taskStart);
    frame->cs = 0x08;
    frame->ss = 0x10;
    frame->rflags = 0x202;
    frame->rsp = sp + sizeof(Interrupt::Frame);
    frame->ss = 0x10;

    t->context = sp;
    return t;
}

void Task::taskDestroy(Task* task)
{
    if (!task) return;
    if (task->kernelStack) SlabAllocator::free(reinterpret_cast<void*>(task->kernelStack));

    SlabAllocator::free(task);
}

extern "C" void taskExit()
{
    CPU* cpu = CPUManager::getCurrentCPU();
    auto* scheduler = cpu->scheduler;

    Task::Task* current = cpu->currentTask;
    if (!current || current == scheduler->idleTask) while (true) asm volatile("hlt");

    current->state = Task::TaskState::DEAD;
    current->next = scheduler->head;
    scheduler->head = current;

    Task::Task* next = Scheduler::pickNextTask(scheduler);
    cpu->currentTask = next;
    scheduler->currentTask = next;

    Interrupt::disableInterrupts();
    while (true) asm volatile("hlt");
}

extern "C" void taskTrampoline(Task::Task* task)
{
    if (!task || !task->entry)
    {
        taskExit();
        return;
    }

    task->state = Task::TaskState::RUNNING;
    task->entry(task->arg);

    taskExit();
}
