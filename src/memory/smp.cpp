#include <memory/smp.h>
#include <memory/paging.h>
#include <drivers/video/renderer.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <boot/limine.h>

extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;

extern "C" void trampoline();
Spinlock SMP::smpLock;

alignas(4096) static uint8_t kernelStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
alignas(4096) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
static ApBootInfo apBoot[SMP::MAX_CPUS] = {};
static uint32_t apCount = 0;

Atomic SMP::apReadyCount{0};
uint32_t SMP::cpuCount = 0, cpuIDs[SMP::MAX_CPUS] = {};
uint64_t SMP::lapicPhysBase = 0, SMP::lapicVirtBase = 0;

extern "C" [[noreturn]] void apMain(uint32_t cpuID)
{
    GDTManager::load();
    IDTManager::load();
    GDTManager::loadTR(cpuID);

    SMP::apReadyCount.increment();
    Renderer::printf("\x1b[33m[AP] Core %u online!\x1b[0m\n", cpuID);

    asm volatile ("sti");
    while (true) asm volatile ("hlt");
}

void SMP::init()
{
    LockGuard guard(smpLock);

    if (!smp_request.response || smp_request.response->cpu_count == 0) return;
    cpuCount = smp_request.response->cpu_count;

    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
    lapicPhysBase = (static_cast<uint64_t>(high) << 32) | low;
    lapicPhysBase &= 0xFFFFF000;

    lapicVirtBase = lapicPhysBase + hhdm_request.response->offset;
    if (!Paging::mapSmall(lapicVirtBase, lapicPhysBase, PageFlags::PRESENT | PageFlags::RW))
    {
        Renderer::printf("\x1b[31m[SMP] Failed to map LAPIC MMIO!\x1b[0m\n");
        return;
    }

    uint32_t logicalID = 0;
    apCount = 0;

    for (uint32_t i = 0; i < cpuCount; ++i)
    {
        auto* cpu = smp_request.response->cpus[i];
        if (!cpu) continue;

        const bool isBSP = cpu->lapic_id == smp_request.response->bsp_lapic_id;
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
            cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);

            Renderer::printf("\x1b[33m[AP] Queued core (LAPIC ID %u) as CPU ID %u\x1b[0m\n", cpu->lapic_id, logicalID);
            apCount++;
        }

        logicalID++;
    }

    cpuCount = logicalID;
    waitForAPs();
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getLapicID()
{
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    if (((static_cast<uint64_t>(high) << 32) | low) & (1ULL << 10))
    {
        uint32_t idLow, idHigh;
        asm volatile("rdmsr" : "=a"(idLow), "=d"(idHigh) : "c"(0x802));

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

void SMP::waitForAPs()
{
    int timeout = 1000000;
    uint32_t expectedAPs = apCount;

    Renderer::printf("\x1b[36m[SMP] \x1b[96mWaiting for %u APs to come online...\x1b[0m\n", expectedAPs);
    while (apReadyCount.load() < expectedAPs && timeout--) asm volatile ("pause");

    if (timeout <= 0)
    {
        Renderer::printf("\x1b[31mTimeout waiting for APs! Using %u cores instead of %u.\x1b[0m\n",
                         apReadyCount.load() + 1, cpuCount);
        cpuCount = apReadyCount.load() + 1;
    }
    else Renderer::printf("\x1b[32mAll %u cores online!\x1b[0m\n", cpuCount);
}
