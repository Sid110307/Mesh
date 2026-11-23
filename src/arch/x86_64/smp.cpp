#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <arch/x86_64/smp.h>
#include <core/limine.h>
#include <core/panic.h>
#include <drivers/renderer.h>
#include <memory/atomic.h>
#include <memory/paging.h>
#include <memory/vmm.h>

struct ApBootInfo
{
    uint64_t stackTop;
    uint32_t cpuId, reserved;
    uint64_t cr3;
};

extern limine_hhdm_request hhdm_request;
extern limine_mp_request mp_request;
extern "C" void trampoline();

ApBootInfo apBoot[SMP::MAX_CPUS] = {};
uint64_t kernelStackTops[SMP::MAX_CPUS] = {};
Atomic<uint32_t> apReadyCount{0};
SMP::CPUFeatures cpuFeatures = {};
uint32_t cpuCount = 1;
uint64_t lapicVirtBase = 0;
bool apicReady = false;
uint32_t tlbPending[SMP::MAX_CPUS] = {}, stopPending[SMP::MAX_CPUS] = {}, stopped[SMP::MAX_CPUS] = {};

void flushLocal()
{
    uint64_t cr4, cr3;
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    asm volatile ("mov %0, %%cr4" :: "r"(cr4 & ~(1ULL << 7)) : "memory");
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    asm volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
    asm volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
}

extern "C" void apMain(uint32_t cpuId)
{
    Paging::activate();
    GDTManager::load();
    IDTManager::load();
    GDTManager::loadTR(cpuId);

    if (!CPUManager::initCPU(cpuId, SMP::getLapicId()))
        Panic::panic("Failed to initialize CPU %u (LAPIC ID %u)!",
                     cpuId, SMP::getLapicId());
    LAPIC::init(SMP::getLapicBase());

    if (!CPUManager::initRuntime(cpuId)) Panic::panic("Failed to initialize CPU %u runtime!", cpuId);
    LAPIC::timerInit(0x22);
    LAPIC::timerSetDivide(16);
    LAPIC::timerPeriodic();
    CPUManager::getCurrentCPU()->timerReady = true;

    apReadyCount.increment();
    Interrupt::enableInterrupts();

    while (true) asm volatile ("hlt");
}

void SMP::init()
{
    if (!mp_request.response || !mp_request.response->cpu_count) Panic::panic("No CPUs detected in MP table!");

    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    const uint64_t physicalBase = ((static_cast<uint64_t>(high) << 32) | low) & 0x000FFFFFFFFFF000ULL;
    lapicVirtBase = physicalBase + hhdm_request.response->offset;

    if (!(low & (1u << 10)) && !Paging::map(lapicVirtBase, physicalBase, FrameAllocator::SMALL_SIZE,
                                            PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE |
                                            PageFlags::WRITE_THROUGH | PageFlags::NO_EXECUTE))
        Panic::panic("Failed to map LAPIC MMIO!");
    low |= 1u << 11;
    asm volatile ("wrmsr" :: "c"(0x1B), "a"(low), "d"(high));

    LAPIC::init(lapicVirtBase);
    __atomic_store_n(&apicReady, true, __ATOMIC_RELEASE);
    LAPIC::timerInit(0x22);
    LAPIC::timerSetDivide(16);
    LAPIC::timerCalibrate(10);
    LAPIC::timerPeriodic();

    CPUManager::getCurrentCPU()->timerReady = true;
    uint32_t logicalId = 1;

    for (uint64_t i = 0; i < mp_request.response->cpu_count; ++i)
    {
        auto* info = mp_request.response->cpus[i];
        if (!info || info->lapic_id == mp_request.response->bsp_lapic_id) continue;
        if (logicalId == MAX_CPUS) break;

        const uint64_t top = getKernelStackTop(logicalId);
        GDTManager::setTSS(logicalId, top);
        apBoot[logicalId] = {top, logicalId, 0, Paging::rootPhysical()};
        info->extra_argument = reinterpret_cast<uint64_t>(&apBoot[logicalId]);

        __atomic_store_n(&info->goto_address, reinterpret_cast<uint8_t*>(trampoline), __ATOMIC_RELEASE);
        ++logicalId;
    }

    const uint32_t expected = logicalId - 1;
    for (uint32_t elapsed = 0; apReadyCount.load() < expected && elapsed < 5000; ++elapsed) LAPIC::waitMs(1);
    if (apReadyCount.load() != expected)
        Panic::panic("Timeout waiting for APs to boot! Only %u/%u APs are online.",
                     apReadyCount.load(), expected);

    cpuCount = logicalId;
    cpuCount = apReadyCount.load() + 1;
    Renderer::printf("\x1b[32mAll %u cores online!\x1b[0m\n", cpuCount);
}

void SMP::detectCPUFeatures()
{
    uint32_t eax, ebx, ecx, edx;
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));

    cpuFeatures.hasSSE = edx & (1u << 25);
    cpuFeatures.hasSSE2 = edx & (1u << 26);
    cpuFeatures.hasSSE3 = ecx & (1u << 0);
    cpuFeatures.hasSSE4_1 = ecx & (1u << 19);
    cpuFeatures.hasSSE4_2 = ecx & (1u << 20);
    cpuFeatures.hasXSAVE = ecx & (1u << 26);
    cpuFeatures.hasOSXSAVE = ecx & (1u << 27);
    cpuFeatures.hasAVX = ecx & (1u << 28);
    cpuFeatures.hasX2APIC = ecx & (1u << 21);
    cpuFeatures.hasTSCDeadline = ecx & (1u << 24);
    cpuFeatures.hasPAT = edx & (1u << 16);
    cpuFeatures.hasAVXUsable = false;

    if (cpuFeatures.hasOSXSAVE)
    {
        uint32_t low, high;
        asm volatile ("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        cpuFeatures.hasAVXUsable = cpuFeatures.hasAVX && (((static_cast<uint64_t>(high) << 32) | low) & 0x6) == 0x6;
    }

    uint32_t maxExtended;
    asm volatile ("cpuid" : "=a"(maxExtended), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000), "c"(0));
    cpuFeatures.hasNX = false;

    if (maxExtended >= 0x80000001)
    {
        asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001), "c"(0));
        cpuFeatures.hasNX = edx & (1u << 20);
    }
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getLapicId()
{
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    const uint32_t id = LAPIC::read(0x20);
    return low & (1u << 10) ? id : id >> 24;
}

uint64_t SMP::getKernelStackTop(const uint32_t cpuId)
{
    if (cpuId >= MAX_CPUS) Panic::panic("Invalid CPU ID %u", cpuId);
    if (!kernelStackTops[cpuId])
    {
        void* stack = VMM::allocateStack(SMP_STACK_SIZE);
        if (!stack) Panic::panic("Failed to allocate kernel stack for CPU %u", cpuId);
        kernelStackTops[cpuId] = reinterpret_cast<uint64_t>(stack) + FrameAllocator::SMALL_SIZE + SMP_STACK_SIZE;
    }
    return kernelStackTops[cpuId];
}

uint64_t SMP::getLapicBase() { return lapicVirtBase; }
SMP::CPUFeatures SMP::getCPUFeatures() { return cpuFeatures; }

bool SMP::handleNmi()
{
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000101));
    if (!(low | high)) return false;

    CPU* cpu = CPUManager::getCurrentCPU();
    if (!cpu || cpu->id >= MAX_CPUS) return false;

    const uint32_t id = cpu->id;
    if (__atomic_load_n(&stopPending[id], __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&stopped[id], 1u, __ATOMIC_RELEASE);
        while (true) asm volatile ("cli; hlt");
    }

    if (__atomic_load_n(&tlbPending[id], __ATOMIC_ACQUIRE))
    {
        flushLocal();
        __atomic_store_n(&tlbPending[id], 0u, __ATOMIC_RELEASE);

        return true;
    }

    return false;
}

void SMP::shootdown()
{
    flushLocal();
    if (!__atomic_load_n(&apicReady, __ATOMIC_ACQUIRE)) return;
    CPU* self = CPUManager::getCurrentCPU();

    for (uint32_t i = 0; i < MAX_CPUS; ++i)
    {
        if (&cpus[i] == self || !__atomic_load_n(&cpus[i].online, __ATOMIC_ACQUIRE)) continue;
        __atomic_store_n(&tlbPending[i], 1u, __ATOMIC_RELEASE);

        LAPIC::sendNmi(cpus[i].lapicId);
        uint32_t budget = 100000000;

        while (__atomic_load_n(&tlbPending[i], __ATOMIC_ACQUIRE) && --budget) asm volatile ("pause");
        if (!budget) Panic::panic("Timeout waiting for CPU %u to flush TLB!", i);
    }
}

void SMP::stopOthers()
{
    if (!__atomic_load_n(&apicReady, __ATOMIC_ACQUIRE)) return;
    CPU* self = CPUManager::getCurrentCPU();

    for (uint32_t i = 0; i < MAX_CPUS; ++i)
    {
        if (&cpus[i] == self || !__atomic_load_n(&cpus[i].started, __ATOMIC_ACQUIRE)) continue;
        __atomic_store_n(&stopPending[i], 1u, __ATOMIC_RELEASE);
        LAPIC::sendNmi(cpus[i].lapicId);
    }

    for (uint32_t i = 0; i < MAX_CPUS; ++i)
    {
        if (&cpus[i] == self || !__atomic_load_n(&cpus[i].started, __ATOMIC_ACQUIRE)) continue;
        uint32_t budget = 10000000;
        while (!__atomic_load_n(&stopped[i], __ATOMIC_ACQUIRE) && --budget) asm volatile ("pause");
    }
}
