#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <drivers/serial.h>
#include <memory/atomic.h>

constexpr uint32_t REG_SVR = 0xF0, REG_EOI = 0xB0, REG_LVT_TIMER = 0x320,
                   REG_TIMER_INITIAL_COUNT = 0x380, REG_TIMER_CURRENT_COUNT = 0x390,
                   REG_TIMER_DIVIDE_CONFIG = 0x3E0;

volatile uint32_t *lapicRegisters = nullptr, *ioapicRegisters = nullptr;
uint32_t globalIrqBase = 0;

Atomic apicTicks{0};
uint16_t apicTimerPort = 0;
uint32_t apicTimerFrequency = 0, apicTimerTick = 0;

uint32_t getDivide(const uint8_t divide)
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

uint32_t timerRead() { return apicTimerPort == 0 ? 0 : inl(apicTimerPort) & 0x00FFFFFFu; }

void timerWaitTicks(const uint32_t ticks)
{
    const uint32_t start = timerRead();
    while (((timerRead() - start) & 0x00FFFFFFu) < ticks) asm volatile ("pause");
}

void LAPIC::init(const uint64_t virtBase)
{
    lapicRegisters = reinterpret_cast<volatile uint32_t*>(virtBase);
    write(REG_SVR, (read(REG_SVR) & ~0xFFu) | 0xFFu | (1u << 8));
}

void LAPIC::write(const uint32_t reg, const uint32_t value) { lapicRegisters[reg / 4] = value; }
uint32_t LAPIC::read(const uint32_t reg) { return lapicRegisters[reg / 4]; }
void LAPIC::sendEOI() { write(REG_EOI, 0); }

void LAPIC::timerInit(const uint8_t vector)
{
    write(REG_LVT_TIMER, (read(REG_LVT_TIMER) & ~0xFFu) | vector | (1u << 16));
}

void LAPIC::timerSetDivide(const uint8_t divide) { write(REG_TIMER_DIVIDE_CONFIG, getDivide(divide)); }

void LAPIC::timerOneShot()
{
    uint32_t lvt = read(REG_LVT_TIMER);
    lvt &= ~(1u << 17);
    lvt &= ~(1u << 16);

    write(REG_LVT_TIMER, lvt);
    write(REG_TIMER_INITIAL_COUNT, apicTimerTick);
}

void LAPIC::timerPeriodic()
{
    uint32_t lvt = read(REG_LVT_TIMER);
    lvt |= 1u << 17;
    lvt &= ~(1u << 16);

    write(REG_LVT_TIMER, lvt);
    write(REG_TIMER_INITIAL_COUNT, apicTimerTick);
}

void LAPIC::timerCalibrate(const uint32_t sampleMs)
{
    if (apicTimerPort == 0 || sampleMs == 0) return;

    write(REG_TIMER_INITIAL_COUNT, 0xFFFFFFFFu);
    timerWaitTicks(3579545u * sampleMs / 1000u);

    apicTimerFrequency = (0xFFFFFFFFu - read(REG_TIMER_CURRENT_COUNT)) * 1000u / sampleMs;
    apicTimerTick = apicTimerFrequency / 1000u;

    if (apicTimerTick == 0)
    {
        Serial::printf("LAPIC: Timer frequency too low to generate 1ms ticks (frequency %u Hz)\n", apicTimerFrequency);
        apicTimerTick = 1;
    }
}

void LAPIC::timerIrq() { apicTicks.increment(); }
void LAPIC::timerSetPort(const uint16_t port) { apicTimerPort = port; }
uint64_t LAPIC::timerGetTicks() { return apicTicks.load(); }

void LAPIC::sleepMs(uint32_t ms)
{
    if (ms == 0 || apicTimerFrequency == 0) return;
    if (!Interrupt::interruptsEnabled())
    {
        Serial::printf("LAPIC: Cannot sleep with interrupts disabled.\n");
        return;
    }

    const uint64_t endTime = timerGetTicks() + ms;
    while (timerGetTicks() < endTime) asm volatile ("hlt");
}

void IOAPIC::init(const uint64_t virtBase, const uint32_t irqBase)
{
    ioapicRegisters = reinterpret_cast<volatile uint32_t*>(virtBase);
    globalIrqBase = irqBase;
}

void IOAPIC::redirect(const uint32_t irq, const uint8_t vector, const uint8_t lapicId, const bool activeLow,
                      const bool levelTriggered)
{
    if (irq < globalIrqBase) return;

    const uint32_t reg = 0x10 + (irq - globalIrqBase) * 2;
    const uint64_t entry = vector | (activeLow ? 1ULL << 13 : 0) | (levelTriggered ? 1ULL << 15 : 0) | (
        static_cast<uint64_t>(lapicId) << 56);

    write(reg, static_cast<uint32_t>(entry & 0xFFFFFFFF));
    write(reg + 1, static_cast<uint32_t>(entry >> 32));
}

void IOAPIC::write(const uint32_t reg, const uint32_t value)
{
    ioapicRegisters[0] = reg;
    ioapicRegisters[4] = value;
}

uint32_t IOAPIC::read(const uint32_t reg)
{
    ioapicRegisters[0] = reg;
    return ioapicRegisters[4];
}
