#include <arch/x86_64/cpu.h>

extern Scheduler::Scheduler schedulers[SMP::MAX_CPUS];
CPU cpus[SMP::MAX_CPUS];

void idleTask(void*) { while (true) asm volatile ("hlt"); }

bool CPUManager::initCPU(const uint32_t cpuId, const uint32_t lapicId)
{
    if (cpuId >= SMP::MAX_CPUS) return false;

    CPU* cpu = &cpus[cpuId];
    memset(cpu, 0, sizeof(CPU));

    cpu->self = cpu;
    cpu->id = cpuId;
    cpu->lapicId = lapicId;
    cpu->started = true;
    cpu->online = false;
    cpu->schedulerReady = false;
    cpu->timerReady = false;
    cpu->kernelStackTop = SMP::getKernelStackTop(cpuId);

    const auto v = reinterpret_cast<uint64_t>(cpu);
    asm volatile ("wrmsr" :: "c"(0xC0000101), "a"(static_cast<uint32_t>(v & 0xFFFFFFFFu)), "d"(static_cast<uint32_t>(v
        >> 32)));

    return true;
}

bool CPUManager::initRuntime(const uint32_t cpuId)
{
    CPU* cpu = getCurrentCPU();
    if (!cpu || cpu->id != cpuId) return false;

    cpu->scheduler = &schedulers[cpuId];
    cpu->scheduler->cpuId = cpuId;

    if (!cpu->idleTask)
    {
        Task::Task* idle = Task::taskCreate(idleTask, nullptr, 0);
        if (!idle) return false;

        cpu->idleTask = idle;
    }

    cpu->currentTask = cpu->idleTask;
    Scheduler::initCPU(cpu->scheduler, cpu->idleTask);
    cpu->schedulerReady = true;
    cpu->online = true;

    return true;
}

CPU* CPUManager::getCurrentCPU()
{
    CPU* cpu;
    asm volatile ("mov %%gs:0, %0" : "=r"(cpu));

    return cpu;
}

uint32_t CPUManager::getCurrentCPUId()
{
    const CPU* cpu = getCurrentCPU();
    return cpu ? cpu->id : 0;
}
