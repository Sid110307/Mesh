#include <arch/x86_64/idt.h>
#include <arch/x86_64/isr.h>

struct __attribute__ ((packed)) IDTEntry
{
    uint16_t offsetLow, selector;
    uint8_t ist, typeAttr;
    uint16_t offsetMid;
    uint32_t offsetHigh, zero;
};

struct __attribute__ ((packed)) IDTPointer
{
    uint16_t limit;
    uint64_t base;
};

IDTEntry idt[256];
IDTPointer idtPointer = {sizeof(idt) - 1, reinterpret_cast<uint64_t>(idt)};

extern "C" void* const isrList[32] = {
    reinterpret_cast<void*>(isr0), reinterpret_cast<void*>(isr1), reinterpret_cast<void*>(isr2),
    reinterpret_cast<void*>(isr3), reinterpret_cast<void*>(isr4), reinterpret_cast<void*>(isr5),
    reinterpret_cast<void*>(isr6), reinterpret_cast<void*>(isr7), reinterpret_cast<void*>(isr8),
    reinterpret_cast<void*>(isr9), reinterpret_cast<void*>(isr10), reinterpret_cast<void*>(isr11),
    reinterpret_cast<void*>(isr12), reinterpret_cast<void*>(isr13), reinterpret_cast<void*>(isr14),
    reinterpret_cast<void*>(isr15), reinterpret_cast<void*>(isr16), reinterpret_cast<void*>(isr17),
    reinterpret_cast<void*>(isr18), reinterpret_cast<void*>(isr19), reinterpret_cast<void*>(isr20),
    reinterpret_cast<void*>(isr21), reinterpret_cast<void*>(isr22), reinterpret_cast<void*>(isr23),
    reinterpret_cast<void*>(isr24), reinterpret_cast<void*>(isr25), reinterpret_cast<void*>(isr26),
    reinterpret_cast<void*>(isr27), reinterpret_cast<void*>(isr28), reinterpret_cast<void*>(isr29),
    reinterpret_cast<void*>(isr30), reinterpret_cast<void*>(isr31),
};

void IDTManager::init()
{
    for (int i = 0; i < 32; ++i)
    {
        uint8_t ist = 0;
        switch (i)
        {
            case 8:
                ist = 1;
                break;
            case 14:
                ist = 2;
                break;
            case 2:
                ist = 3;
                break;
            case 18:
                ist = 4;
                break;
            case 12:
                ist = 5;
                break;
            default:
                ist = 0;
                break;
        }

        setEntry(i, reinterpret_cast<void (*)()>(isrList[i]), 0x8E, ist);
    }
}

void IDTManager::load() { asm volatile ("lidt %0" :: "m"(idtPointer) : "memory"); }

void IDTManager::setEntry(const uint8_t vector, void (*isr)(), const uint8_t flags, const uint8_t ist)
{
    if (!isr)
    {
        idt[vector] = {};
        return;
    }

    const auto addr = reinterpret_cast<uint64_t>(isr);
    idt[vector] = {
        .offsetLow = static_cast<uint16_t>(addr & 0xFFFF),
        .selector = 0x08,
        .ist = ist,
        .typeAttr = flags,
        .offsetMid = static_cast<uint16_t>(addr >> 16 & 0xFFFF),
        .offsetHigh = static_cast<uint32_t>(addr >> 32 & 0xFFFFFFFF),
        .zero = 0
    };
}
