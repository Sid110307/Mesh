#include <arch/x86_64/gdt.h>
#include <memory/smp.h>
#include <core/utils.h>
#include <boot/limine.h>

extern limine_hhdm_request hhdm_request;

static constexpr uint16_t GDT_NULL = 0, GDT_CODE = 1, GDT_DATA = 2, GDT_TSS = 3;
static constexpr size_t TSS_ENTRIES = SMP::MAX_CPUS * 2, BASE_ENTRIES = 5, GDT_ENTRIES = BASE_ENTRIES + TSS_ENTRIES;

extern "C" {
GDTEntry gdt[GDT_ENTRIES] __attribute__((aligned(8))) = {};
GDTPointer gdtPointer = {};
}

static uint8_t istStacks[SMP::MAX_CPUS][7][8192] __attribute__((aligned(16)));
TSS kernelTSS[SMP::MAX_CPUS] __attribute__((aligned(16)));

GDTManager::GDTManager()
{
    setEntry(GDT_NULL, 0, 0, 0, 0);
    setEntry(GDT_CODE, 0, 0, 0x9A, 0x20);
    setEntry(GDT_DATA, 0, 0, 0x92, 0x00);
    setEntry(GDT_TSS, 0, 0, 0x89, 0x00);
    setEntry(GDT_TSS + 1, 0, 0, 0x00, 0x00);

    gdtPointer.limit = sizeof(gdt) - 1;
    gdtPointer.base = reinterpret_cast<uint64_t>(gdt);
}

void GDTManager::load()
{
    uint16_t dataSel = GDT_DATA << 3;
    uint32_t codeSel = GDT_CODE << 3;

    asm volatile ("lgdt %0" :: "m"(gdtPointer) : "memory");
    asm volatile ("mov %[sel], %%ax\nmov %%ax, %%ds\nmov %%ax, %%es\nmov %%ax, %%fs\nmov %%ax, %%gs\nmov %%ax, %%ss\n"
        :: [sel] "r"(dataSel) : "rax");
    asm volatile ("mov %[cs], %%eax\npushq %%rax\nlea 1f(%%rip), %%rax\npushq %%rax\nretfq\n1:\n" :: [cs] "r"(codeSel) :
        "rax", "memory");
}

void GDTManager::setTSS(const size_t cpuIndex, const uint64_t rsp0)
{
    memset(&kernelTSS[cpuIndex], 0, sizeof(TSS));
    kernelTSS[cpuIndex].rsp[0] = rsp0;
    kernelTSS[cpuIndex].ioMapBase = sizeof(TSS);
    for (int i = 0; i < 7; ++i)
        kernelTSS[cpuIndex].ist[i] = reinterpret_cast<uint64_t>(&istStacks[cpuIndex][i]) + sizeof(istStacks[cpuIndex][
            i]);

    const auto base = reinterpret_cast<uint64_t>(&kernelTSS[cpuIndex]);
    const size_t gdtIndex = GDT_TSS + cpuIndex * 2;
    setEntry(gdtIndex, base & 0xFFFFFFFF, sizeof(TSS) - 1, 0x89, 0x00);

    *reinterpret_cast<uint32_t*>(&gdt[gdtIndex + 1]) = (base >> 32) & 0xFFFFFFFF;
    *(reinterpret_cast<uint32_t*>(&gdt[gdtIndex + 1]) + 1) = 0;
}

void GDTManager::loadTR(size_t cpuIndex)
{
    asm volatile ("ltr %0" :: "r"(static_cast<uint16_t>((GDT_TSS + cpuIndex * 2) << 3)) : "memory");
}

void GDTManager::setEntry(const uint16_t index, const uint32_t base, const uint32_t limit, const uint8_t access,
                          const uint8_t flags)
{
    gdt[index].limitLow = limit & 0xFFFF;
    gdt[index].baseLow = base & 0xFFFF;
    gdt[index].baseMid = (base >> 16) & 0xFF;
    gdt[index].access = access;
    gdt[index].flagsLimitHigh = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[index].baseHigh = (base >> 24) & 0xFF;
}
