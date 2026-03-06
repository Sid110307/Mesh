#include <drivers/serial.h>
#include <memory/slab.h>
#include <memory/vmm.h>

constexpr uint64_t KERNEL_HEAP_BASE = 0xFFFF800000000000ULL, KERNEL_HEAP_SIZE = 0x0000000200000000ULL; // 8 GiB
constexpr uint64_t VMALLOC_BASE = 0xFFFF802000000000ULL, VMALLOC_SIZE = 0x0000000400000000ULL; // 16 GiB
constexpr uint64_t MMIO_BASE = 0xFFFF806000000000ULL, MMIO_SIZE = 0x0000000200000000ULL; // 8 GiB
constexpr uint64_t KERNEL_STACK_BASE = 0xFFFF808000000000ULL, KERNEL_STACK_SIZE = 0x0000000100000000ULL; // 4 GiB

Spinlock vmmLock;
VMM::Region* regionHead = nullptr;

VMM::RegionNode* nodeFromRegion(VMM::Region* region)
{
    return reinterpret_cast<VMM::RegionNode*>(reinterpret_cast<uint8_t*>(region) - offsetof(VMM::RegionNode, region));
}

const VMM::RegionNode* nodeFromRegion(const VMM::Region* region)
{
    return reinterpret_cast<const VMM::RegionNode*>(reinterpret_cast<const uint8_t*>(region) - offsetof(
        VMM::RegionNode, region));
}

bool regionContains(const VMM::Region* region, const uint64_t base, const uint64_t size)
{
    return region && base >= region->base && base + size <= region->base + region->size;
}

VMM::Region* findRegion(const uint64_t base, const uint64_t size)
{
    VMM::Region* current = regionHead;
    while (current)
    {
        if (current->base == base && current->size == size) return current;
        current = current->next;
    }

    return nullptr;
}

VMM::Region* findRegionByBase(const uint64_t base)
{
    VMM::Region* current = regionHead;
    while (current)
    {
        if (current->base == base) return current;
        current = current->next;
    }

    return nullptr;
}

void insertRegion(VMM::Region* region)
{
    if (!regionHead)
    {
        regionHead = region;
        region->prev = region->next = nullptr;

        return;
    }

    VMM::Region *current = regionHead, *previous = nullptr;
    while (current && current->base < region->base)
    {
        previous = current;
        current = current->next;
    }

    region->prev = previous;
    region->next = current;

    if (previous) previous->next = region;
    else regionHead = region;

    if (current) current->prev = region;
}

void removeRegion(VMM::Region* region)
{
    if (!region) return;

    if (region->prev) region->prev->next = region->next;
    else regionHead = region->next;

    if (region->next) region->next->prev = region->prev;
    region->prev = region->next = nullptr;
}

uint64_t findFreeRange(const uint64_t windowBase, const uint64_t windowSize, const uint64_t size,
                       const uint64_t alignment)
{
    const uint64_t alignedBase = Alignment::alignUp(windowBase, alignment);
    if (alignedBase < windowBase) return 0;

    uint64_t currentBase = alignedBase;
    const VMM::Region* current = regionHead;

    while (current)
    {
        if (!Alignment::overlaps(currentBase, size, current->base, current->size) &&
            current->base >= currentBase + size)
            return currentBase;

        if (current->base + current->size > currentBase)
        {
            currentBase = Alignment::alignUp(current->base + current->size, alignment);
            if (currentBase < windowBase || currentBase >= windowBase + windowSize) return 0;
        }
        current = current->next;
    }
    return currentBase + size <= windowBase + windowSize ? currentBase : 0;
}

void windowForType(const VMM::RegionType type, uint64_t& base, uint64_t& size)
{
    switch (type)
    {
        case VMM::RegionType::HEAP:
        case VMM::RegionType::RESERVED:
            base = KERNEL_HEAP_BASE;
            size = KERNEL_HEAP_SIZE;

            break;
        case VMM::RegionType::STACK:
            base = KERNEL_STACK_BASE;
            size = KERNEL_STACK_SIZE;

            break;
        case VMM::RegionType::MMIO:
        case VMM::RegionType::FRAMEBUFFER:
        case VMM::RegionType::DIRECT:
            base = MMIO_BASE;
            size = MMIO_SIZE;

            break;
        case VMM::RegionType::ANONYMOUS:
        default:
            base = VMALLOC_BASE;
            size = VMALLOC_SIZE;

            break;
    }
}

const char* regionTypeName(const VMM::RegionType type)
{
    switch (type)
    {
        case VMM::RegionType::ANONYMOUS: return "ANONYMOUS";
        case VMM::RegionType::HEAP: return "HEAP";
        case VMM::RegionType::STACK: return "STACK";
        case VMM::RegionType::MMIO: return "MMIO";
        case VMM::RegionType::FRAMEBUFFER: return "FRAMEBUFFER";
        case VMM::RegionType::DIRECT: return "DIRECT";
        case VMM::RegionType::RESERVED: return "RESERVED";
        default: return "UNKNOWN";
    }
}

bool remap(VMM::RegionNode* node, const PageFlags newFlags)
{
    if (!node || !node->region.committed) return false;

    const uint64_t base = node->region.base, size = node->region.size, pageCount = node->pageCount;
    const PageFlags mapFlags = newFlags | PageFlags::PRESENT;

    Paging::unmap(base, size);
    for (uint64_t i = 0; i < pageCount; ++i)
    {
        const uint64_t phys = node->region.directMapped
                                  ? node->physicalBase + i * FrameAllocator::SMALL_SIZE
                                  : node->pages
                                  ? node->pages[i]
                                  : 0;
        if (!phys || !Paging::map(base + i * FrameAllocator::SMALL_SIZE, phys, FrameAllocator::SMALL_SIZE, mapFlags))
        {
            for (uint64_t j = 0; j < i; ++j)
                Paging::map(base + j * FrameAllocator::SMALL_SIZE,
                            node->region.directMapped
                                ? node->physicalBase + j * FrameAllocator::SMALL_SIZE
                                : node->pages
                                ? node->pages[j]
                                : 0, FrameAllocator::SMALL_SIZE, node->region.flags | PageFlags::PRESENT);
            return false;
        }
    }

    node->region.flags = newFlags;
    return true;
}

bool VMM::init()
{
    LockGuard guard(vmmLock);
    regionHead = nullptr;

    return true;
}

void* VMM::reserve(uint64_t size, const RegionType type, const PageFlags flags, uint64_t alignment)
{
    if (size == 0) return nullptr;
    if (alignment < FrameAllocator::SMALL_SIZE) alignment = FrameAllocator::SMALL_SIZE;
    if ((alignment & (alignment - 1)) != 0) return nullptr;

    size = Alignment::alignUp(size, FrameAllocator::SMALL_SIZE);
    uint64_t windowBase = 0, windowSize = 0;
    windowForType(type, windowBase, windowSize);

    LockGuard guard(vmmLock);
    const uint64_t base = findFreeRange(windowBase, windowSize, size, alignment);
    if (!base) return nullptr;

    auto* node = static_cast<RegionNode*>(SlabAllocator::alloc(sizeof(RegionNode), alignof(RegionNode)));
    if (!node) return nullptr;
    memset(node, 0, sizeof(RegionNode));

    node->region.base = base;
    node->region.size = size;
    node->region.type = type;
    node->region.flags = flags & ~(PageFlags::PRESENT | PageFlags::HUGE | PageFlags::ACCESSED | PageFlags::DIRTY);
    node->region.committed = false;
    node->region.directMapped = false;
    node->pageCount = size / FrameAllocator::SMALL_SIZE;
    node->physicalBase = 0;
    node->ownedPhysical = false;

    insertRegion(&node->region);
    return reinterpret_cast<void*>(base);
}

void VMM::dumpRegions()
{
    LockGuard guard(vmmLock);
    Serial::printf("VMM Regions:\n");
    for (const Region* current = regionHead; current; current = current->next)
        Serial::printf("  Base: 0x%lx, Size: 0x%lx, Type: %s, Flags: 0x%lx, Committed: %s, Direct Mapped: %s\n",
                       current->base, current->size, regionTypeName(current->type), current->flags,
                       current->committed ? "Yes" : "No", current->directMapped ? "Yes" : "No");
}

bool VMM::commit(void* base)
{
    if (!base) return false;

    const auto start = reinterpret_cast<uint64_t>(base);
    if (!Alignment::aligned(start, FrameAllocator::SMALL_SIZE)) return false;

    LockGuard guard(vmmLock);
    Region* region = findRegionByBase(start);

    if (!region) return false;
    auto* node = nodeFromRegion(region);
    if (region->committed || region->directMapped) return false;
    if (!node || node->pageCount == 0) return false;

    node->pages = static_cast<uint64_t*>(SlabAllocator::alloc(node->pageCount * sizeof(uint64_t), alignof(uint64_t)));
    if (!node->pages) return false;
    memset(node->pages, 0, node->pageCount * sizeof(uint64_t));

    const PageFlags mapFlags = region->flags | PageFlags::PRESENT;
    uint64_t mappedPages = 0;

    for (uint64_t i = 0; i < node->pageCount; ++i)
    {
        void* frame = FrameAllocator::alloc();
        if (!frame)
        {
            for (uint64_t j = 0; j < mappedPages; ++j)
            {
                Paging::unmap(region->base + j * FrameAllocator::SMALL_SIZE, FrameAllocator::SMALL_SIZE);
                if (node->pages[j]) FrameAllocator::free(reinterpret_cast<void*>(node->pages[j]));
                node->pages[j] = 0;
            }

            SlabAllocator::free(node->pages);
            node->pages = nullptr;

            return false;
        }

        const auto phys = reinterpret_cast<uint64_t>(frame);
        if (!Paging::map(region->base + i * FrameAllocator::SMALL_SIZE, phys, FrameAllocator::SMALL_SIZE, mapFlags))
        {
            FrameAllocator::free(frame);
            for (uint64_t j = 0; j < mappedPages; ++j)
            {
                Paging::unmap(region->base + j * FrameAllocator::SMALL_SIZE, FrameAllocator::SMALL_SIZE);
                if (node->pages[j]) FrameAllocator::free(reinterpret_cast<void*>(node->pages[j]));
                node->pages[j] = 0;
            }

            SlabAllocator::free(node->pages);
            node->pages = nullptr;

            return false;
        }

        node->pages[i] = phys;
        mappedPages++;
    }

    node->ownedPhysical = true;
    region->committed = true;
    region->directMapped = false;

    return true;
}

bool VMM::protect(void* base, const PageFlags flags)
{
    if (!base) return false;
    const auto start = reinterpret_cast<uint64_t>(base);
    if (!Alignment::aligned(start, FrameAllocator::SMALL_SIZE)) return false;

    LockGuard guard(vmmLock);
    Region* region = findRegionByBase(start);

    return region && region->base == start
               ? remap(nodeFromRegion(region),
                       flags & ~(PageFlags::PRESENT | PageFlags::HUGE | PageFlags::ACCESSED | PageFlags::DIRTY))
               : false;
}

bool VMM::map(void* virtualAddress, const uint64_t physicalAddress, uint64_t size, const RegionType type,
              const PageFlags flags)
{
    if (!virtualAddress || size == 0) return false;

    const auto virt = Alignment::alignDown(reinterpret_cast<uint64_t>(virtualAddress), FrameAllocator::SMALL_SIZE),
               phys = Alignment::alignDown(physicalAddress, FrameAllocator::SMALL_SIZE);
    size = Alignment::alignUp(size + (reinterpret_cast<uint64_t>(virtualAddress) - virt), FrameAllocator::SMALL_SIZE);
    LockGuard guard(vmmLock);

    bool created = false;
    RegionNode* createdNode = nullptr;
    Region* region = findRegion(virt, size);

    if (!region)
    {
        uint64_t windowBase = 0, windowSize = 0;
        windowForType(type, windowBase, windowSize);

        if (virt < windowBase || virt + size > windowBase + windowSize) return false;
        for (const Region* current = regionHead; current; current = current->next)
            if (Alignment::overlaps(virt, size, current->base, current->size)) return false;

        auto* node = static_cast<RegionNode*>(SlabAllocator::alloc(sizeof(RegionNode), alignof(RegionNode)));
        if (!node) return false;
        memset(node, 0, sizeof(RegionNode));

        node->region.base = virt;
        node->region.size = size;
        node->region.type = type;
        node->region.flags = flags & ~(PageFlags::PRESENT | PageFlags::HUGE | PageFlags::ACCESSED | PageFlags::DIRTY);
        node->region.committed = false;
        node->region.directMapped = false;
        node->pageCount = size / FrameAllocator::SMALL_SIZE;

        insertRegion(&node->region);
        region = &node->region;
        created = true;
        createdNode = node;
    }

    auto* node = nodeFromRegion(region);
    if (region->committed || region->directMapped) return false;
    if (region->base != virt || region->size != size) return false;

    if (!Paging::map(virt, phys, size, flags | PageFlags::PRESENT))
    {
        if (created)
        {
            removeRegion(&createdNode->region);
            SlabAllocator::free(createdNode);
        }

        return false;
    }

    node->physicalBase = phys;
    node->ownedPhysical = false;
    region->committed = true;
    region->directMapped = true;

    return true;
}

bool VMM::unmap(void* base)
{
    if (!base) return false;

    const auto start = reinterpret_cast<uint64_t>(base);
    if (!Alignment::aligned(start, FrameAllocator::SMALL_SIZE)) return false;

    LockGuard guard(vmmLock);
    Region* region = findRegionByBase(start);
    if (!region) return false;

    auto* node = nodeFromRegion(region);
    if (region->committed && node && node->region.committed)
    {
        if (node->ownedPhysical && node->pages)
            for (uint64_t i = 0; i < node->pageCount; ++i)
                if (node->pages[i]) FrameAllocator::free(reinterpret_cast<void*>(node->pages[i]));

        Paging::unmap(node->region.base, node->region.size);
        node->region.committed = false;
    }

    if (node && node->pages)
    {
        SlabAllocator::free(node->pages);
        node->pages = nullptr;
    }

    removeRegion(region);
    SlabAllocator::free(node);

    return true;
}
