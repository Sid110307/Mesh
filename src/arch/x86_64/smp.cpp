#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/smp.h>
#include <core/limine.h>
#include <drivers/renderer.h>
#include <memory/atomic.h>
#include <memory/paging.h>
#include <memory/spinlock.h>

struct ApBootInfo
{
    uint64_t stackTop;
    uint32_t cpuId;
};

extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern limine_mp_request mp_request;
extern "C" void trampoline();

alignas(FrameAllocator::SMALL_SIZE) static uint8_t kernelStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
alignas(FrameAllocator::SMALL_SIZE) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];

Spinlock smpLock;
ApBootInfo apBoot[SMP::MAX_CPUS] = {};
uint32_t apCount = 0;
Atomic apReadyCount{0};
SMP::CPUFeatures cpuFeatures = {};
uint32_t cpuCount = 0, cpuIds[SMP::MAX_CPUS] = {};
uint64_t lapicPhysBase = 0, lapicVirtBase = 0;

extern "C" [[noreturn]] void apMain(uint32_t cpuId)
{
    GDTManager::load();
    IDTManager::load();
    GDTManager::loadTR(cpuId);
    CPUManager::initCPU(cpuId, SMP::getLapicId());

    apReadyCount.increment();
    Interrupt::enableInterrupts();

    while (true) asm volatile ("hlt");
}

void SMP::init()
{
    LockGuard guard(smpLock);

    if (!mp_request.response || mp_request.response->cpu_count == 0) return;
    cpuCount = mp_request.response->cpu_count;

    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
    lapicPhysBase = (static_cast<uint64_t>(high) << 32 | low) & 0xFFFFF000;
    lapicVirtBase = lapicPhysBase + hhdm_request.response->offset;

    if (!Paging::map(lapicVirtBase, lapicPhysBase, FrameAllocator::SMALL_SIZE,
                     PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE | PageFlags::WRITE_THROUGH |
                     PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Renderer::printf("\x1b[31m[SMP] Failed to map LAPIC MMIO!\x1b[0m\n");
        return;
    }

    uint32_t logicalId = 0;
    apCount = 0;

    for (uint32_t i = 0; i < cpuCount; ++i)
    {
        auto* cpu = mp_request.response->cpus[i];
        if (!cpu) continue;

        const bool isBSP = cpu->lapic_id == mp_request.response->bsp_lapic_id;
        if (!isBSP && logicalId >= MAX_CPUS)
        {
            Renderer::printf("\x1b[31m[SMP] Too many CPUs! Ignoring LAPIC ID %u.\x1b[0m\n", cpu->lapic_id);
            continue;
        }

        cpuIds[logicalId] = cpu->lapic_id;
        GDTManager::setTSS(logicalId, reinterpret_cast<uint64_t>(&kernelStacks[logicalId][SMP_STACK_SIZE]));

        if (!isBSP)
        {
            apBoot[logicalId].stackTop = (reinterpret_cast<uint64_t>(&apStacks[logicalId][0]) + SMP_STACK_SIZE) & ~
                0xFULL;
            apBoot[logicalId].cpuId = logicalId;

            cpu->extra_argument = reinterpret_cast<uint64_t>(&apBoot[logicalId]);
            __atomic_thread_fence(__ATOMIC_RELEASE);
            cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);

            Renderer::printf("\x1b[33m[AP] Queued core (LAPIC ID %u) as CPU ID %u.\x1b[0m\n", cpu->lapic_id, logicalId);
            apCount++;
        }

        logicalId++;
    }

    cpuCount = logicalId;
    uint32_t expectedAPs = apCount;

    int timeout = 1000000;
    while (apReadyCount.load() < expectedAPs && timeout--) asm volatile ("pause");

    if (timeout <= 0)
    {
        Renderer::printf("\x1b[31mTimeout waiting for APs! Using %u cores instead of %u.\x1b[0m\n",
                         apReadyCount.load() + 1, cpuCount);
        cpuCount = apReadyCount.load() + 1;
    }
    else Renderer::printf("\x1b[32mAll %u cores online!\x1b[0m\n", cpuCount);
}

void SMP::detectCPUFeatures()
{
    uint32_t eax, ebx, ecx, edx;
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));

    cpuFeatures.hasSSE = edx & (1 << 25);
    cpuFeatures.hasSSE2 = edx & (1 << 26);
    cpuFeatures.hasSSE3 = ecx & (1 << 0);
    cpuFeatures.hasSSE4_1 = ecx & (1 << 19);
    cpuFeatures.hasSSE4_2 = ecx & (1 << 20);
    cpuFeatures.hasAVX = ecx & (1 << 27);
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getLapicId()
{
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    if ((static_cast<uint64_t>(high) << 32 | low) & 1ULL << 10)
    {
        uint32_t idLow, idHigh;
        asm volatile ("rdmsr" : "=a"(idLow), "=d"(idHigh) : "c"(0x802));

        return idLow;
    }

    auto* lapic = reinterpret_cast<volatile uint32_t*>(lapicVirtBase);
    return lapic[0x20 / 4] >> 24;
}

uint64_t SMP::getKernelStackTop(const uint32_t cpuId)
{
    if (cpuId >= cpuCount) return 0;
    return reinterpret_cast<uint64_t>(&kernelStacks[cpuId][SMP_STACK_SIZE]);
}

uint64_t SMP::getLapicBase() { return lapicVirtBase; }
SMP::CPUFeatures SMP::getCPUFeatures() { return cpuFeatures; }
