#pragma once

#include <kernel/core/utils.h>

class LAPIC
{
public:
    static void init(uint64_t virtBase);
    static void write(uint32_t reg, uint32_t value);
    static uint32_t read(uint32_t reg);
    static void sendEOI();

private:
    static constexpr uint32_t REGISTER_SVR = 0xF0, REGISTER_EOI = 0xB0;
    static inline volatile uint32_t* registers = nullptr;
};

class IOAPIC
{
public:
    static void init(uint64_t virtBase, uint32_t irqBase);
    static void redirect(uint32_t irq, uint8_t vector, uint8_t lapicId, bool activeLow, bool levelTriggered);

    static void write(uint32_t reg, uint32_t value);
    static uint32_t read(uint32_t reg);

private:
    static inline volatile uint32_t* registers = nullptr;
    static inline uint32_t globalIrqBase = 0;
};
