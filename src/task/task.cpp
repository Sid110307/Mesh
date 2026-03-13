#include <arch/x86_64/cpu.h>
#include <arch/x86_64/isr.h>
#include <memory/atomic.h>
#include <memory/slab.h>
#include <memory/vmm.h>
#include <task/scheduler.h>
#include <task/task.h>

Atomic<uint32_t> nextTaskId{0};

extern "C" void taskTrampoline(Task::Task* task);

extern "C" void taskExit()
{
    CPU* cpu = CPUManager::getCurrentCPU();
    auto* scheduler = cpu->scheduler;

    Task::Task* current = cpu->currentTask;
    if (!current || current == scheduler->idleTask) while (true) asm volatile ("hlt");

    Interrupt::disableInterrupts();
    current->state = Task::TaskState::DEAD;
    current->next = scheduler->deadHead;
    scheduler->deadHead = current;

    Task::taskYield();
    while (true) asm volatile ("hlt");
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

extern "C" void taskStart(Task::Task* task)
{
    taskTrampoline(task);
    taskExit();
}

Task::Task* Task::taskCreate(void (*entry)(void*), void* arg, int priority)
{
    if (!entry) return nullptr;
    if (priority < 0) priority = 0;
    if (priority > MAX_PRIORITY) priority = MAX_PRIORITY;

    auto* t = static_cast<Task*>(SlabAllocator::alloc(sizeof(Task), alignof(Task)));
    if (!t) return nullptr;
    memset(t, 0, sizeof(Task));

    t->id = nextTaskId.increment();
    t->state = TaskState::READY;
    t->priority = priority;
    t->timeSlice = DEFAULT_TIME_SLICE;
    t->entry = entry;
    t->arg = arg;
    t->ownedCpuId = CPUManager::getCurrentCPUId();
    t->queued = false;
    t->next = nullptr;
    t->prev = nullptr;
    t->kernelStackSize = 16384;

    void* guardPage = VMM::reserve(FrameAllocator::SMALL_SIZE, VMM::RegionType::STACK,
                                   PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE);
    if (!guardPage)
    {
        SlabAllocator::free(t);
        return nullptr;
    }

    void* stackRegion = VMM::reserve(t->kernelStackSize, VMM::RegionType::STACK,
                                     PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE);
    if (!stackRegion)
    {
        VMM::unmap(guardPage);
        SlabAllocator::free(t);

        return nullptr;
    }

    if (!VMM::commit(stackRegion))
    {
        VMM::unmap(stackRegion);
        VMM::unmap(guardPage);
        SlabAllocator::free(t);

        return nullptr;
    }

    t->kernelStackBase = reinterpret_cast<uint64_t>(guardPage);
    t->kernelStackRegion = reinterpret_cast<uint64_t>(stackRegion);

    const uint64_t sp = ((reinterpret_cast<uint64_t>(stackRegion) + t->kernelStackSize) & ~0xFULL) -
        sizeof(Interrupt::TimerFrame);
    auto* frame = reinterpret_cast<Interrupt::TimerFrame*>(sp);
    memset(frame, 0, sizeof(*frame));

    frame->rdi = reinterpret_cast<uint64_t>(t);
    frame->rip = reinterpret_cast<uint64_t>(taskStart);
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rsp = reinterpret_cast<uint64_t>(stackRegion) + t->kernelStackSize;
    frame->ss = 0x10;

    t->context = sp;
    return t;
}

void Task::taskDestroy(Task* task)
{
    if (!task) return;
    if (task->kernelStackRegion) VMM::unmap(reinterpret_cast<void*>(task->kernelStackRegion));
    if (task->kernelStackBase) VMM::unmap(reinterpret_cast<void*>(task->kernelStackBase));

    SlabAllocator::free(task);
}

void Task::taskYield() { asm volatile ("int $0x80" ::: "memory"); }
