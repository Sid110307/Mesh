#pragma once

#include <core/utils.h>

namespace BuddyAllocator
{
    bool init();

    uint64_t alloc(int order);
    void free(uint64_t address, int order);
    void reserve(uint64_t address, uint64_t size);

    uint64_t getTotalPages();
    uint64_t getFreePages();
    uint64_t getBaseAddress();
    uint64_t getSize();
}
