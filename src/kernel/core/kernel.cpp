#include <drivers/video/renderer.h>
#include <drivers/io/keyboard/keyboard.h>

#include <kernel/arch/gdt.h>
#include <kernel/arch/idt.h>
#include <kernel/arch/isr.h>
#include <kernel/arch/acpi.h>
#include <memory/paging.h>
#include <memory/smp.h>
#include <memory/lapic.h>
#include <kernel/boot/limine.h>

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memmap_request;
extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern limine_mp_request mp_request;
extern limine_date_at_boot_request date_at_boot_request;

void initSSE()
{
    uint64_t cr0, cr4;

    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= 1ULL << 1;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("fninit");
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
}

void initGDT()
{
    Renderer::printf("\x1b[36mInitializing GDT... ");

    [[maybe_unused]] GDTManager gdtManager;
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
    IDTManager::load();
    Renderer::printf("\x1b[32mDone!\n");
}

void initPaging()
{
    Renderer::printf("\x1b[36mInitializing Paging... ");
    FrameAllocator::init();
    Paging::init();
    Renderer::printf("\x1b[32mDone!\n");
}

void initAPIC()
{
    Renderer::printf("\x1b[36mChecking LAPIC status... ");

    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    uint64_t apicBase = static_cast<uint64_t>(high) << 32 | low;
    bool apic = apicBase & 1ULL << 11;
    bool x2apic = apicBase & 1ULL << 10;

    if (apic)
    {
        Renderer::printf("\x1b[32mEnabled\x1b[0m");
        if (x2apic) Renderer::printf(" \x1b[90m(x2APIC)\x1b[0m");
        Renderer::printf("\n");
    }
    else Renderer::printf("\x1b[31mDisabled\x1b[0m\n");
}

void initIOAPIC()
{
    Renderer::printf("\x1b[36mInitializing IOAPIC... ");

    ACPI::MADTInfo madt = {};
    if (!ACPI::init(madt))
    {
        Renderer::printf("\x1b[31mFailed to initialize ACPI/MADT\x1b[0m\n");
        return;
    }

    const uint64_t ioapicVirt = madt.ioapicPhys + hhdm_request.response->offset;
    if (!Paging::mapSmall(ioapicVirt, madt.ioapicPhys,
                          PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE | PageFlags::WRITE_THROUGH |
                          PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Renderer::printf("\x1b[31mFailed to map IOAPIC MMIO\x1b[0m\n");
        return;
    }

    IOAPIC::init(ioapicVirt, madt.ioapicGlobalIrqBase);
    IOAPIC::redirect(madt.hasIso ? madt.irq1GlobalIrqBase : 1, 0x21,
                     static_cast<uint8_t>(mp_request.response->bsp_lapic_id), madt.irq1ActiveLow,
                     madt.irq1LevelTriggered);

    Renderer::printf("\x1b[32mDone!\x1b[0m\n");
}

extern "C" [[noreturn]] void kernelMain()
{
    initSSE();
    initRenderer();
    Renderer::setSerialPrint(true);
    dumpStats();
    initPaging();
    initGDT();
    initIDT();
    initAPIC();
    SMP::init();
    LAPIC::init(SMP::getLapicBase());
    initIOAPIC();
    Keyboard::init();

    asm volatile ("sti");
    while (true)
    {
        while (char c = Keyboard::readChar()) Renderer::printf("%c", c);
        asm volatile ("hlt");
    }
}
