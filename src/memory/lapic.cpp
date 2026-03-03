#include <memory/lapic.h>

constexpr uint32_t REGISTER_SVR = 0xF0, REGISTER_EOI = 0xB0;
volatile uint32_t *lapicRegisters = nullptr, *ioapicRegisters = nullptr;
uint32_t globalIrqBase = 0;

void LAPIC::init(const uint64_t virtBase)
{
    lapicRegisters = reinterpret_cast<volatile uint32_t*>(virtBase);
    write(REGISTER_SVR, (read(REGISTER_SVR) & ~0xFFu) | 0xFFu | (1u << 8));
}

void LAPIC::write(const uint32_t reg, const uint32_t value) { lapicRegisters[reg / 4] = value; }
uint32_t LAPIC::read(const uint32_t reg) { return lapicRegisters[reg / 4]; }
void LAPIC::sendEOI() { write(REGISTER_EOI, 0); }

void IOAPIC::init(const uint64_t virtBase, const uint32_t irqBase)
{
    ioapicRegisters = reinterpret_cast<volatile uint32_t*>(virtBase);
    globalIrqBase = irqBase;
}

void IOAPIC::redirect(const uint32_t irq, const uint8_t vector, const uint8_t lapicId, const bool activeLow,
                      const bool levelTriggered)
{
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
