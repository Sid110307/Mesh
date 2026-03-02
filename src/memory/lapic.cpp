#include <memory/lapic.h>

void LAPIC::init(const uint64_t virtBase)
{
    registers = reinterpret_cast<volatile uint32_t*>(virtBase);
    write(REGISTER_SVR, (read(REGISTER_SVR) & ~0xFFu) | 0xFFu | (1u << 8));
}

void LAPIC::write(const uint32_t reg, const uint32_t value) { registers[reg / 4] = value; }
uint32_t LAPIC::read(const uint32_t reg) { return registers[reg / 4]; }
void LAPIC::sendEOI() { write(REGISTER_EOI, 0); }

void IOAPIC::init(const uint64_t virtBase, const uint32_t irqBase)
{
    registers = reinterpret_cast<volatile uint32_t*>(virtBase);
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
    registers[0] = reg;
    registers[4] = value;
}

uint32_t IOAPIC::read(const uint32_t reg)
{
    registers[0] = reg;
    return registers[4];
}
