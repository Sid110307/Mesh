#include <arch/x86_64/cpu.h>

CPU cpus[SMP::MAX_CPUS];

void CPUManager::initCPU(const uint32_t cpuId, const uint32_t lapicId)
{
    if (cpuId >= SMP::MAX_CPUS) return;

    CPU* cpu = &cpus[cpuId];
    cpu->self = cpu;
    cpu->id = cpuId;
    cpu->lapicId = lapicId;
    cpu->started = true;
    cpu->ticks = 0;
    cpu->preemptedTasks = 0;

    const auto v = reinterpret_cast<uint64_t>(cpu);
    asm volatile ("wrmsr" :: "c"(0xC0000101), "a"(static_cast<uint32_t>(v & 0xFFFFFFFFu)), "d"(static_cast<uint32_t>(v >>
        32)));
}

CPU* CPUManager::getCurrentCPU()
{
    CPU* cpu;
    asm volatile ("mov %%gs:0, %0" : "=r"(cpu));

    return cpu;
}

uint32_t CPUManager::getCurrentCPUId() { return getCurrentCPU()->id; }
