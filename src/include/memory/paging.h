#pragma once

#include <core/utils.h>
#include <memory/spinlock.h>

enum class PageFlags : uint64_t
{
    NONE = 0,
    PRESENT = 1ULL << 0,
    RW = 1ULL << 1,
    USER = 1ULL << 2,
    WRITE_THROUGH = 1ULL << 3,
    CACHE_DISABLE = 1ULL << 4,
    ACCESSED = 1ULL << 5,
    DIRTY = 1ULL << 6,
    HUGE = 1ULL << 7,
    GLOBAL = 1ULL << 8,
    NO_EXECUTE = 1ULL << 63
};

constexpr PageFlags operator|(PageFlags a, PageFlags b)
{
    return static_cast<PageFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

constexpr PageFlags operator&(PageFlags a, PageFlags b)
{
    return static_cast<PageFlags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

constexpr PageFlags operator~(PageFlags flag) { return static_cast<PageFlags>(~static_cast<uint64_t>(flag)); }

namespace Paging
{
    void init();

    bool map(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t size, PageFlags flags);
    void unmap(uint64_t virtualAddress, uint64_t size);
}

namespace FrameAllocator
{
    void init();
    void* alloc();
    void free(void* frame);
    void reserve(void* frame);
    bool used(void* frame);

    uint64_t usedCount();
    uint64_t totalCount();

    constexpr uint64_t SMALL_SIZE = 4096, MEDIUM_SIZE = 2ULL * 1024 * 1024, LARGE_SIZE = 1ULL * 1024 * 1024 * 1024;
}
