#pragma once

#include <core/utils.h>
#include <memory/paging.h>

namespace VMM
{
    enum class RegionType : uint8_t
    {
        ANONYMOUS,
        HEAP,
        STACK,
        MMIO,
        FRAMEBUFFER,
        DIRECT,
        RESERVED
    };

    struct Region
    {
        uint64_t base = 0, size = 0;
        RegionType type = RegionType::RESERVED;
        PageFlags flags = PageFlags::NONE;
        bool committed = false, directMapped = false;
        Region *next = nullptr, *prev = nullptr;
    };

    struct RegionNode
    {
        Region region = {};
        uint64_t physicalBase = 0, pageCount = 0;
        uint64_t* pages = nullptr;
        bool ownedPhysical = false;
    };

    bool init();
    void* reserve(uint64_t size, RegionType type, PageFlags flags, uint64_t alignment = FrameAllocator::SMALL_SIZE);
    void dumpRegions();

    bool commit(void* base);
    bool protect(void* base, PageFlags flags);

    bool map(void* virtualAddress, uint64_t physicalAddress, uint64_t size, RegionType type, PageFlags flags);
    bool unmap(void* base);
}
