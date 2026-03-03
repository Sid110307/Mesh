#pragma once

#include <kernel/core/utils.h>

namespace IDTManager
{
    void init();
    void load();
    void setEntry(uint8_t vector, void (*isr)(), uint8_t flags = 0x8E, uint8_t ist = 0);
}
