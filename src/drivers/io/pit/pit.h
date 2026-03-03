#pragma once

#include <kernel/core/utils.h>
#include <kernel/sync/atomic.h>

class PIT
{
public:
    static void init(uint32_t freq);
    static void irq();

    static void sleepMs(uint32_t ms);
    static uint64_t getTicks();

private:
    static Atomic ticks;
    static uint32_t frequency;

    static constexpr uint32_t PIT_BASE_FREQUENCY = 1193182;
    static constexpr uint16_t PIT_CHANNEL0 = 0x40, PIT_COMMAND = 0x43;

    static void setDivisor(uint16_t divisor);
};
