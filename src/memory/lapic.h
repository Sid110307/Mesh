#pragma once

#include <kernel/core/utils.h>

namespace LAPIC
{
    void init(uint64_t virtBase);
    void write(uint32_t reg, uint32_t value);
    uint32_t read(uint32_t reg);
    void sendEOI();
}

namespace IOAPIC
{
    void init(uint64_t virtBase, uint32_t irqBase);
    void redirect(uint32_t irq, uint8_t vector, uint8_t lapicId, bool activeLow, bool levelTriggered);
    void write(uint32_t reg, uint32_t value);
    uint32_t read(uint32_t reg);
}
