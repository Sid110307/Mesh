#include <core/limine.h>
#include <core/utils.h>
#include <drivers/serial.h>
#include <memory/buddy.h>
#include <memory/paging.h>
#include <memory/slab.h>
#include <memory/spinlock.h>

struct SlabCache;

struct SlabHeader
{
    uint32_t magic;
    uint16_t inUse, total;
    SlabHeader* next;
    SlabCache* cache;
    void* freeList;
};

struct SlabCache
{
    const char* name = nullptr;
    size_t objectSize = 0, alignment = 0;
    Spinlock lock;
    SlabHeader* partial = nullptr;
};

struct BigHeader
{
    uint32_t magic;
    uint16_t order, reserved;
    size_t objectSize;
};

extern limine_hhdm_request hhdm_request;

constexpr uint32_t SLAB_MAGIC = 0xDEADBEEF, BIG_MAGIC = 0xB16B00B5;

constexpr size_t classes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096},
                 numClasses = sizeof(classes) / sizeof(classes[0]);
SlabCache slabCaches[numClasses] = {};

void* physicalToVirtual(const uint64_t phys) { return reinterpret_cast<void*>(phys + hhdm_request.response->offset); }

uint64_t virtualToPhysical(void* virt)
{
    const auto v = reinterpret_cast<uint64_t>(virt);
    return v >= hhdm_request.response->offset ? v - hhdm_request.response->offset : v;
}

SlabCache* findCache(const size_t size, const size_t alignment)
{
    size_t need = size;

    if (alignment > need) need = alignment;
    for (size_t i = 0; i < numClasses; ++i) if (classes[i] >= need) return &slabCaches[i];

    return nullptr;
}

SlabHeader* newSlab(SlabCache* cache)
{
    const uint64_t slabPhys = BuddyAllocator::alloc(0);
    if (!slabPhys) return nullptr;

    auto* baseAddress = static_cast<uint8_t*>(physicalToVirtual(slabPhys));
    auto* header = reinterpret_cast<SlabHeader*>(baseAddress);

    memset(header, 0, sizeof(SlabHeader));
    header->magic = SLAB_MAGIC;
    header->cache = cache;

    uint64_t objStart = reinterpret_cast<uint64_t>(baseAddress) + sizeof(SlabHeader);
    objStart = Alignment::alignUp(objStart, cache->alignment);

    const uint64_t end = reinterpret_cast<uint64_t>(baseAddress) + FrameAllocator::SMALL_SIZE,
                   space = objStart < end ? end - objStart : 0;
    const auto total = static_cast<uint16_t>(space / cache->objectSize);

    header->total = total;
    header->inUse = 0;
    header->freeList = nullptr;

    const auto p = reinterpret_cast<uint8_t*>(objStart);
    for (uint16_t i = 0; i < total; ++i)
    {
        void* obj = p + i * cache->objectSize;
        *static_cast<void**>(obj) = header->freeList;
        header->freeList = obj;
    }

    header->next = nullptr;
    return header;
}

void* allocateSlab(SlabCache* cache)
{
    LockGuard guard(cache->lock);

    SlabHeader* slab = cache->partial;
    while (slab && slab->freeList == nullptr) slab = slab->next;

    if (!slab)
    {
        slab = newSlab(cache);
        if (!slab) return nullptr;

        slab->next = cache->partial;
        cache->partial = slab;
    }

    void* obj = slab->freeList;
    if (!obj) return nullptr;
    slab->freeList = *static_cast<void**>(obj);
    ++slab->inUse;

    return obj;
}

void freeSlab(SlabHeader* slab, void* obj)
{
    LockGuard guard(slab->cache->lock);

    *static_cast<void**>(obj) = slab->freeList;
    slab->freeList = obj;
    if (slab->inUse) --slab->inUse;

    if (slab->inUse == 0)
    {
        SlabHeader** current = &slab->cache->partial;
        while (*current)
        {
            if (*current == slab)
            {
                *current = slab->next;
                break;
            }
            current = &((*current)->next);
        }

        BuddyAllocator::free(virtualToPhysical(slab), 0);
    }
}

int orderForSize(const size_t size)
{
    int order = 0;
    size_t p = 1;

    while (p < (size + FrameAllocator::SMALL_SIZE - 1) / FrameAllocator::SMALL_SIZE)
    {
        p <<= 1;
        order++;
    }
    return order;
}

bool SlabAllocator::init()
{
    for (size_t i = 0; i < numClasses; ++i)
    {
        slabCaches[i].name = "SlabCache";
        slabCaches[i].objectSize = classes[i];
        slabCaches[i].alignment = 16;
        slabCaches[i].partial = nullptr;
    }

    return true;
}

void* SlabAllocator::alloc(size_t size, size_t alignment)
{
    if (size == 0) size = 1;
    if (alignment < 16) alignment = 16;

    if (SlabCache* cache = findCache(size, alignment)) return allocateSlab(cache);

    const size_t total = sizeof(BigHeader) + size + (alignment - 1);
    const int order = orderForSize(total);

    const uint64_t phys = BuddyAllocator::alloc(order);
    if (!phys) return nullptr;

    auto base = static_cast<uint8_t*>(physicalToVirtual(phys));
    auto* header = reinterpret_cast<BigHeader*>(base);
    header->magic = BIG_MAGIC;
    header->order = static_cast<uint16_t>(order);
    header->objectSize = size;

    return reinterpret_cast<void*>(Alignment::alignUp(reinterpret_cast<uint64_t>(base) + sizeof(BigHeader), alignment));
}

void SlabAllocator::free(void* obj)
{
    if (!obj) return;

    const uint64_t pageBase = Alignment::alignDown(reinterpret_cast<uint64_t>(obj), FrameAllocator::SMALL_SIZE);
    if (auto* slab = static_cast<SlabHeader*>(physicalToVirtual(pageBase)); slab->magic == SLAB_MAGIC && slab->cache)
    {
        freeSlab(slab, obj);
        return;
    }

    if (const auto* big = static_cast<BigHeader*>(physicalToVirtual(pageBase)); big->magic == BIG_MAGIC)
    {
        BuddyAllocator::free(virtualToPhysical(reinterpret_cast<void*>(pageBase)), big->order);
        return;
    }

    Serial::printf("SlabAllocator: Attempted to free invalid pointer %p.\n", obj);
}

size_t SlabAllocator::usableSize(void* obj)
{
    if (!obj) return 0;
    const uint64_t pageBase = Alignment::alignDown(reinterpret_cast<uint64_t>(obj), FrameAllocator::SMALL_SIZE);

    if (const auto* slab = static_cast<SlabHeader*>(physicalToVirtual(pageBase));
        slab->magic == SLAB_MAGIC && slab->cache)
        return slab->cache->objectSize;
    if (const auto* big = static_cast<BigHeader*>(physicalToVirtual(pageBase)); big->magic == BIG_MAGIC)
        return big->objectSize;

    return 0;
}
