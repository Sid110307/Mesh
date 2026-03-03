#pragma once

#include <kernel/core/utils.h>

namespace PIT
{
    void init(uint32_t freq);
    void irq();

    void sleepMs(uint32_t ms);
    uint64_t getTicks();
}
