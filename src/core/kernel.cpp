#include <arch/x86_64/acpi.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <arch/x86_64/smp.h>
#include <core/limine.h>
#include <core/panic.h>
#include <drivers/keyboard.h>
#include <drivers/renderer.h>
#include <memory/buddy.h>
#include <memory/paging.h>
#include <memory/slab.h>

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memmap_request;
extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern limine_mp_request mp_request;
extern limine_date_at_boot_request date_at_boot_request;

extern "C" void isrTimer();
extern "C" void isrYield();

Scheduler::Scheduler schedulers[SMP::MAX_CPUS];
void idleTask(void*) { while (true) asm volatile ("hlt"); }

void initSIMD()
{
    SMP::detectCPUFeatures();
    if (!SMP::getCPUFeatures().hasSSE)
    {
        Renderer::printf("\x1b[31mCPU does not support SSE.\x1b[0m\n");
        return;
    }

    uint64_t cr0, cr4;

    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1) | (1ULL << 5);
    asm volatile ("mov %0, %%cr0" :: "r"(cr0));

    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);
    asm volatile ("mov %0, %%cr4" :: "r"(cr4));

    asm volatile ("fninit");
}

void initRenderer()
{
    Renderer::init();
    Renderer::clear(BLACK);
    Renderer::printf("\x1b[92mMesh Booted Successfully!\n");
}

void dumpStats()
{
    if (framebuffer_request.response)
    {
        Renderer::printf("\x1b[36m[Framebuffer] \x1b[96m%ux%u\x1b[0m\n",
                         framebuffer_request.response->framebuffers[0]->width,
                         framebuffer_request.response->framebuffers[0]->height);
        Renderer::printf("\x1b[36m[Framebuffer] Supported Video Modes:\n");
        for (size_t i = 0; i < framebuffer_request.response->framebuffer_count; ++i)
        {
            const auto fb = framebuffer_request.response->framebuffers[i];
            Renderer::printf(" \x1b[90m-\x1b[0m %ux%u @ %u bpp\x1b[0m\n", fb->width, fb->height, fb->bpp);
        }
    }
    if (memmap_request.response)
        Renderer::printf("\x1b[36m[Memory Map] \x1b[96m%u entries\x1b[0m\n", memmap_request.response->entry_count);
    if (hhdm_request.response)
        Renderer::printf("\x1b[36m[HHDM Base] \x1b[96m0x%lx\x1b[0m\n", hhdm_request.response->offset);
    if (executable_addr_request.response)
        Renderer::printf(
            "\x1b[36m[Kernel Range]\n \x1b[90m-\x1b[0m Physical: 0x%lx\n \x1b[90m-\x1b[0m Virtual: 0x%lx\n",
            executable_addr_request.response->physical_base, executable_addr_request.response->virtual_base);
    if (date_at_boot_request.response)
        Renderer::printf("\x1b[36m[Boot Time] \x1b[96m%lu\x1b[0m\n", date_at_boot_request.response->timestamp);
    if (mp_request.response && mp_request.response->cpu_count > 0)
        Renderer::printf("\x1b[36m[SMP] \x1b[96m%u CPUs Detected\x1b[0m\n", mp_request.response->cpu_count);

    const auto cpuFeatures = SMP::getCPUFeatures();
    Renderer::printf("\x1b[36m[CPU Features] \x1b[32m%s%s%s%s%s%s\x1b[0m\n", cpuFeatures.hasSSE ? "SSE " : "",
                     cpuFeatures.hasSSE2 ? "SSE2 " : "", cpuFeatures.hasSSE3 ? "SSE3 " : "",
                     cpuFeatures.hasSSE4_1 ? "SSE4.1 " : "", cpuFeatures.hasSSE4_2 ? "SSE4.2 " : "",
                     cpuFeatures.hasAVX ? "AVX " : "");
}

void initGDT()
{
    Renderer::printf("\x1b[36mInitializing GDT... ");
    GDTManager::init();
    GDTManager::load();
    GDTManager::setTSS(0, SMP::getKernelStackTop(0));
    GDTManager::loadTR(0);
    Renderer::printf("\x1b[32mDone!\n");
}

void initIDT()
{
    Renderer::printf("\x1b[36mInitializing IDT... ");
    IDTManager::init();
    IDTManager::setEntry(0x21, reinterpret_cast<void(*)()>(isrKeyboard), 0x8E, 0);
    IDTManager::setEntry(0x22, isrTimer, 0x8E, 0);
    IDTManager::setEntry(0x80, isrYield, 0x8E, 0);
    IDTManager::load();
    Renderer::printf("\x1b[32mDone!\n");
}

void initPaging()
{
    Renderer::printf("\x1b[36mInitializing Paging... ");
    if (!FrameAllocator::init()) Panic::panic("Failed to initialize FrameAllocator.");
    if (!Paging::init()) Panic::panic("Failed to initialize Paging.");
    if (!BuddyAllocator::init()) Panic::panic("Failed to initialize BuddyAllocator.");
    if (!SlabAllocator::init()) Panic::panic("Failed to initialize SlabAllocator.");
    Renderer::printf("\x1b[32mDone!\n");
}

void initIOAPIC()
{
    Renderer::printf("\x1b[36mInitializing IOAPIC... ");

    ACPI::MADTInfo madt = {};
    if (!ACPI::init(madt))
    {
        Renderer::printf("\x1b[31mFailed to initialize ACPI/MADT.\x1b[0m\n");
        return;
    }

    const uint64_t ioapicVirt = madt.ioapicPhys + hhdm_request.response->offset;
    if (!Paging::map(ioapicVirt, madt.ioapicPhys, FrameAllocator::SMALL_SIZE,
                     PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE | PageFlags::WRITE_THROUGH |
                     PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Renderer::printf("\x1b[31mFailed to map IOAPIC MMIO.\x1b[0m\n");
        return;
    }

    IOAPIC::init(ioapicVirt, madt.ioapicGlobalIrqBase);
    const auto lapicId = static_cast<uint8_t>(mp_request.response->bsp_lapic_id);
    uint32_t globalIrq;
    bool activeLow, levelTriggered;

    ACPI::resolveIsa(madt, 1, globalIrq, activeLow, levelTriggered);
    IOAPIC::redirect(globalIrq, 0x21, lapicId, activeLow, levelTriggered);

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    Renderer::printf("\x1b[32mDone!\x1b[0m\n");
}

extern "C" [[noreturn]] void kernelMain()
{
    initRenderer();
    initSIMD();
    Renderer::setSerialPrint(true);
    dumpStats();

    initPaging();
    initGDT();
    initIDT();
    SMP::init();
    LAPIC::init(SMP::getLapicBase());
    CPUManager::initCPU(0, mp_request.response->bsp_lapic_id);

    CPU* cpu = CPUManager::getCurrentCPU();
    cpu->scheduler = &schedulers[0];

    Task::Task* idle = Task::taskCreate(idleTask, nullptr, 0);
    cpu->idleTask = idle;
    cpu->currentTask = idle;
    Scheduler::initCPU(cpu->scheduler, idle);

    initIOAPIC();
    Keyboard::init();

    LAPIC::timerInit(0x22);
    LAPIC::timerSetDivide(16);
    LAPIC::timerCalibrate(10);
    LAPIC::timerPeriodic();

    Interrupt::enableInterrupts();
    while (true)
    {
        while (char c = Keyboard::readChar()) Renderer::printf("%c", c);

        static uint64_t last = 0;
        if (const uint64_t now = LAPIC::timerGetTicks(); now / 1000 != last / 1000)
        {
            Renderer::printf("\x1b[90m.\x1b[0m");
            last = now;
        }

        asm volatile ("hlt");
    }
}
