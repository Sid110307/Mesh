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
    void timerSetPort(uint16_t port);
    uint64_t timerGetTicks();
    void sleepMs(uint32_t ms);
}

namespace IOAPIC
{
    void init(uint64_t virtBase, uint32_t irqBase);
    void redirect(uint32_t irq, uint8_t vector, uint8_t lapicId, bool activeLow, bool levelTriggered);
    void write(uint32_t reg, uint32_t value);
    uint32_t read(uint32_t reg);
}
