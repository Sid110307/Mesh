#pragma once

#include <core/utils.h>

namespace SlabAllocator
{
    bool init();
    void* alloc(size_t size, size_t alignment = 16);
    void free(void* obj);
    size_t usableSize(void* obj);
}
