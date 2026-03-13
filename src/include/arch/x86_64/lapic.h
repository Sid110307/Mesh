#pragma once

#include <core/utils.h>

namespace LAPIC
{
    void init(uint64_t virtBase);
    void write(uint32_t reg, uint32_t value);
    uint32_t read(uint32_t reg);
    void sendEOI();

    void timerInit(uint8_t vector);
    void timerSetDivide(uint8_t divide);
    void timerOneShot();
    void timerPeriodic();
    void timerCalibrate(uint32_t sampleMs);

    void timerIrq();
    uint64_t timerGetTicks();
    uint64_t timerRead();
    void timerSetPort(uint32_t port, bool is32Bit);
    void sleepMs(uint64_t ms);

    constexpr uint32_t REG_SVR = 0xF0, REG_EOI = 0xB0, REG_LVT_TIMER = 0x320, REG_TIMER_INITIAL_COUNT = 0x380,
                       REG_TIMER_CURRENT_COUNT = 0x390, REG_TIMER_DIVIDE_CONFIG = 0x3E0, TIMER_TICKS = 3579545u;
}

namespace IOAPIC
{
    void init(uint64_t virtBase, uint32_t irqBase);
    void redirect(uint32_t irq, uint8_t vector, uint8_t lapicId, bool activeLow, bool levelTriggered);
    void write(uint32_t reg, uint32_t value);
    uint32_t read(uint32_t reg);
}
