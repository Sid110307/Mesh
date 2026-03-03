#include <memory/smp.h>
#include <memory/paging.h>
#include <drivers/video/renderer.h>
#include <kernel/arch/gdt.h>
#include <kernel/arch/idt.h>
#include <kernel/boot/limine.h>

extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern limine_mp_request mp_request;

extern "C" void trampoline();
Spinlock SMP::smpLock;
ApBootInfo SMP::apBoot[MAX_CPUS] = {};
uint32_t SMP::apCount = 0;

alignas(4096) static uint8_t kernelStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
alignas(4096) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];

Atomic SMP::apReadyCount{0};
CPUFeatures SMP::cpuFeatures = {};
uint32_t SMP::cpuCount = 0, cpuIDs[SMP::MAX_CPUS] = {};
uint64_t SMP::lapicPhysBase = 0, SMP::lapicVirtBase = 0;

extern "C" [[noreturn]] void apMain(uint32_t cpuID)
{
    GDTManager::load();
    IDTManager::load();
    GDTManager::loadTR(cpuID);
    SMP::apReadyCount.increment();

    asm volatile ("sti");
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

    if (!Paging::mapSmall(lapicVirtBase, lapicPhysBase,
                          PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE | PageFlags::WRITE_THROUGH |
                          PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Renderer::printf("\x1b[31m[SMP] Failed to map LAPIC MMIO!\x1b[0m\n");
        return;
    }

    uint32_t logicalID = 0;
    apCount = 0;

    for (uint32_t i = 0; i < cpuCount; ++i)
    {
        auto* cpu = mp_request.response->cpus[i];
        if (!cpu) continue;

        const bool isBSP = cpu->lapic_id == mp_request.response->bsp_lapic_id;
        if (!isBSP && logicalID >= MAX_CPUS)
        {
            Renderer::printf("\x1b[31m[SMP] Too many CPUs! Ignoring LAPIC ID %u\x1b[0m\n", cpu->lapic_id);
            continue;
        }

        cpuIDs[logicalID] = cpu->lapic_id;
        GDTManager::setTSS(logicalID, reinterpret_cast<uint64_t>(&kernelStacks[logicalID][SMP_STACK_SIZE]));

        if (!isBSP)
        {
            apBoot[logicalID].stackTop = (reinterpret_cast<uint64_t>(&apStacks[logicalID][0]) + SMP_STACK_SIZE) & ~
                0xFULL;
            apBoot[logicalID].cpuID = logicalID;

            cpu->extra_argument = reinterpret_cast<uint64_t>(&apBoot[logicalID]);
            __atomic_thread_fence(__ATOMIC_RELEASE);
            cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);

            Renderer::printf("\x1b[33m[AP] Queued core (LAPIC ID %u) as CPU ID %u\x1b[0m\n", cpu->lapic_id, logicalID);
            apCount++;
        }

        logicalID++;
    }

    cpuCount = logicalID;
    waitForAPs();
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

uint32_t SMP::getLapicID()
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

uint64_t SMP::getKernelStackTop(const uint32_t cpuID)
{
    if (cpuID >= cpuCount) return 0;
    return reinterpret_cast<uint64_t>(&kernelStacks[cpuID][SMP_STACK_SIZE]);
}

uint64_t SMP::getLapicBase() { return lapicVirtBase; }

void SMP::waitForAPs()
{
    int timeout = 1000000;
    uint32_t expectedAPs = apCount;
    while (apReadyCount.load() < expectedAPs && timeout--) asm volatile ("pause");

    if (timeout <= 0)
    {
        Renderer::printf("\x1b[31mTimeout waiting for APs! Using %u cores instead of %u.\x1b[0m\n",
                         apReadyCount.load() + 1, cpuCount);
        cpuCount = apReadyCount.load() + 1;
    }
    else Renderer::printf("\x1b[32mAll %u cores online!\x1b[0m\n", cpuCount);
}
