#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <drivers/serial.h>
#include <memory/atomic.h>
#include <arch/x86_64/cpu.h>
#include <core/panic.h>
#include <memory/spinlock.h>

volatile uint32_t* lapicRegisters = nullptr;
volatile uint32_t* pmTimerMemory = nullptr;

Atomic<uint64_t> apicTicks{0};
bool isApicTimer32Bit = false;
uint32_t apicTimerPort = 0;
uint64_t apicTimerFrequency = 0, apicTimerTick = 0;

uint64_t getDivide(const uint8_t divide)
{
    switch (divide)
    {
        case 1: return 0b1011;
        case 2: return 0b0000;
        case 4: return 0b0001;
        case 8: return 0b0010;
        case 16: return 0b0011;
        case 32: return 0b1000;
        case 64: return 0b1001;
        case 128: return 0b1010;
        default: return 0b0011;
    }
}

void timerWaitTicks(const uint64_t ticks)
{
    uint64_t elapsed = 0, last = LAPIC::timerRead();
    const uint64_t mask = isApicTimer32Bit ? 0xFFFFFFFFULL : 0xFFFFFFULL;
    uint64_t budget = 100000000;

    while (elapsed < ticks)
    {
        const uint64_t now = LAPIC::timerRead();
        elapsed += (now - last) & mask;
        last = now;

        if (!--budget) Panic::panic("LAPIC: PM timer did not advance");
        asm volatile ("pause");
    }
}

void LAPIC::init(const uint64_t virtBase)
{
    if (!lapicRegisters) lapicRegisters = reinterpret_cast<volatile uint32_t*>(virtBase);
    write(REG_SVR, (read(REG_SVR) & ~0xFFu) | 0xFFu | (1u << 8));
}

void LAPIC::write(const uint32_t reg, const uint32_t value)
{
    uint32_t low, high;

    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
    if (low & (1u << 10)) asm volatile ("wrmsr" :: "c"(0x800 + reg / 16), "a"(value), "d"(0));
    else lapicRegisters[reg / 4] = value;
}

uint32_t LAPIC::read(const uint32_t reg)
{
    uint32_t low, high;

    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
    if (low & (1u << 10))
    {
        asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x800 + reg / 16));
        return low;
    }
    return lapicRegisters[reg / 4];
}

void LAPIC::sendEOI() { write(REG_EOI, 0); }

void LAPIC::timerInit(const uint8_t vector)
{
    write(REG_LVT_TIMER, (read(REG_LVT_TIMER) & ~(0xFFu | (3u << 17))) | vector | (1u << 16));
}

void LAPIC::timerSetDivide(const uint8_t divide) { write(REG_TIMER_DIVIDE_CONFIG, getDivide(divide)); }

void LAPIC::timerOneShot()
{
    uint32_t lvt = read(REG_LVT_TIMER);
    lvt &= ~(3u << 17);
    lvt &= ~(1u << 16);

    write(REG_LVT_TIMER, lvt);
    write(REG_TIMER_INITIAL_COUNT, apicTimerTick);
}

void LAPIC::timerPeriodic()
{
    uint32_t lvt = read(REG_LVT_TIMER);
    lvt &= ~(3u << 17);
    lvt |= 1u << 17;
    lvt &= ~(1u << 16);

    write(REG_LVT_TIMER, lvt);
    write(REG_TIMER_INITIAL_COUNT, apicTimerTick);
}

void LAPIC::timerCalibrate(const uint32_t sampleMs)
{
    if (sampleMs == 0) return;

    write(REG_TIMER_INITIAL_COUNT, 0xFFFFFFFFu);
    waitMs(sampleMs);

    apicTimerFrequency = (0xFFFFFFFFu - read(REG_TIMER_CURRENT_COUNT)) * 1000u / sampleMs;
    apicTimerTick = apicTimerFrequency / 1000u;

    if (apicTimerTick == 0)
    {
        Serial::printf("LAPIC: Timer frequency too low to generate 1ms ticks (frequency %lu Hz)\n", apicTimerFrequency);
        apicTimerTick = 1;
    }
}

void LAPIC::timerIrq()
{
    CPU* cpu = CPUManager::getCurrentCPU();
    if (cpu) __atomic_add_fetch(&cpu->ticks, 1ULL, __ATOMIC_RELAXED);
    if (cpu && cpu->id == 0) apicTicks.increment();
}

uint64_t LAPIC::timerGetTicks() { return apicTicks.load(); }

uint64_t LAPIC::timerRead()
{
    if (!apicTimerPort && !pmTimerMemory) return 0;

    const uint32_t value = pmTimerMemory ? *pmTimerMemory : inl(apicTimerPort);
    return isApicTimer32Bit ? value : (value & 0x00FFFFFFu);
}

void LAPIC::timerSetPort(const uint32_t port, const bool is32Bit)
{
    apicTimerPort = port;
    isApicTimer32Bit = is32Bit;
}

void LAPIC::sleepMs(const uint64_t ms)
{
    CPU* cpu = CPUManager::getCurrentCPU();

    if (!ms || !cpu || !cpu->timerReady || !Interrupt::interruptsEnabled()) return;
    const uint64_t start = __atomic_load_n(&cpu->ticks, __ATOMIC_RELAXED);
    while (__atomic_load_n(&cpu->ticks, __ATOMIC_RELAXED) - start < ms) asm volatile ("hlt");
}

void LAPIC::timerSetMemory(const uint64_t address, const bool is32Bit)
{
    pmTimerMemory = reinterpret_cast<volatile uint32_t*>(address);
    isApicTimer32Bit = is32Bit;
}

void LAPIC::waitMs(uint32_t ms)
{
    while (ms--)
    {
        if (apicTimerPort || pmTimerMemory) timerWaitTicks((TIMER_TICKS + 999) / 1000);
        else
        {
            const uint8_t old = inb(0x61);

            outb(0x61, (old & ~3u));
            outb(0x43, 0xB0);
            outb(0x42, 1193 & 0xFF);
            outb(0x42, 1193 >> 8);
            outb(0x61, (old & ~2u) | 1u);

            uint32_t budget = 10000000;
            while (!(inb(0x61) & 0x20) && --budget) asm volatile ("pause");
            outb(0x61, old);

            if (!budget) Panic::panic("LAPIC: PIT timer did not advance");
        }
    }
}

void LAPIC::sendNmi(const uint32_t lapicId)
{
    asm volatile ("mfence; lfence" ::: "memory");
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));

    if (low & (1u << 10))
        asm volatile ("wrmsr" :: "c"(0x830), "a"(4u << 8), "d"(lapicId));
    else
    {
        uint32_t budget = 10000000;
        while ((read(0x300) & (1u << 12)) && --budget) asm volatile ("pause");
        if (!budget) return;

        write(0x310, lapicId << 24);
        write(0x300, 4u << 8);
        while ((read(0x300) & (1u << 12)) && --budget) asm volatile ("pause");
    }
}

struct IoController
{
    volatile uint32_t* regs;
    uint32_t base, count;
};

IoController controllers[16] = {};
uint32_t controllerCount = 0;
Spinlock ioLock;

uint32_t ioRead(const IoController& c, uint32_t reg)
{
    c.regs[0] = reg;
    return c.regs[4];
}

void ioWrite(const IoController& c, uint32_t reg, uint32_t value)
{
    c.regs[0] = reg;
    c.regs[4] = value;
}

void IOAPIC::init(const uint64_t virtBase, const uint32_t irqBase)
{
    LockGuard guard(ioLock);

    if (controllerCount == 16) Panic::panic("Too many IOAPICs");
    auto& c = controllers[controllerCount++];

    c.regs = reinterpret_cast<volatile uint32_t*>(virtBase);
    c.base = irqBase;
    c.count = ((ioRead(c, 1) >> 16) & 0xFF) + 1;
    for (uint32_t i = 0; i < c.count; ++i) ioWrite(c, 0x10 + i * 2, 1u << 16);
}

void IOAPIC::redirect(const uint32_t irq, const uint8_t vector, const uint8_t lapicId,
                      const bool activeLow, const bool levelTriggered)
{
    LockGuard guard(ioLock);
    for (uint32_t i = 0; i < controllerCount; ++i)
    {
        const auto& c = controllers[i];
        if (irq < c.base || irq - c.base >= c.count) continue;

        const uint32_t reg = 0x10 + (irq - c.base) * 2;
        const uint32_t low = vector | (activeLow ? 1u << 13 : 0) | (levelTriggered ? 1u << 15 : 0);

        ioWrite(c, reg, 1u << 16);
        ioWrite(c, reg + 1, static_cast<uint32_t>(lapicId) << 24);
        ioWrite(c, reg, low);

        return;
    }

    Panic::panic("IOAPIC: IRQ %u not found", irq);
}
