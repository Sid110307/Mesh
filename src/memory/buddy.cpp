#include <core/limine.h>
#include <core/utils.h>
#include <drivers/serial.h>
#include <memory/buddy.h>
#include <memory/paging.h>
#include <memory/spinlock.h>

struct Page
{
    uint16_t order = 0;
    bool free = false, head = false, reserved = false;
    Page *next = nullptr, *prev = nullptr;
};

struct FreeList
{
    Page* head = nullptr;
};

extern limine_hhdm_request hhdm_request;
constexpr int MAX_WANTED_ORDER = 9;

Spinlock buddyLock;
uint64_t buddyBase = 0, buddySize = 0, totalPages = 0, freePages = 0;
int maxOrder = 0;
Page* pages = nullptr;
FreeList* freeLists = nullptr;

uint64_t indexFromAddress(const uint64_t address) { return (address - buddyBase) / FrameAllocator::SMALL_SIZE; }
uint64_t addressFromIndex(const uint64_t index) { return buddyBase + index * FrameAllocator::SMALL_SIZE; }

void listRemove(const int order, Page* page)
{
    if (!page) return;

    if (page->prev) page->prev->next = page->next;
    else freeLists[order].head = page->next;

    if (page->next) page->next->prev = page->prev;
    page->next = page->prev = nullptr;
}

void listAdd(const int order, Page* page)
{
    page->prev = nullptr;
    page->next = freeLists[order].head;
    if (freeLists[order].head) freeLists[order].head->prev = page;

    freeLists[order].head = page;
}

void setBlockState(const uint64_t headIndex, const int order, const bool isFree, const bool isReserved)
{
    for (uint64_t i = 0; i < 1ULL << order; ++i)
    {
        auto& page = pages[headIndex + i];
        page.free = isFree;
        page.reserved = isReserved;
        page.head = i == 0;
        page.order = i == 0 ? static_cast<uint16_t>(order) : 0;
        page.next = page.prev = nullptr;
    }
}

void buildInitialFreeLists(const uint8_t* freeMask)
{
    for (int order = 0; order <= maxOrder; ++order) freeLists[order].head = nullptr;
    freePages = 0;

    uint64_t i = 0;
    while (i < totalPages)
    {
        if (!(freeMask[i / 8] & (1u << (i % 8))))
        {
            ++i;
            continue;
        }

        int best = 0;
        for (int order = maxOrder; order >= 0; --order)
        {
            const uint64_t blockPages = 1ULL << order;
            if ((i & (blockPages - 1)) != 0 || i + blockPages > totalPages) continue;

            bool ok = true;
            for (uint64_t j = 0; j < blockPages; ++j)
                if (!(freeMask[(i + j) / 8] & (1u << ((i + j) % 8))))
                {
                    ok = false;
                    break;
                }

            if (ok)
            {
                best = order;
                break;
            }
        }

        setBlockState(i, best, true, false);
        listAdd(best, &pages[i]);
        freePages += 1ULL << best;
        i += 1ULL << best;
    }
}

void setFreeBit(uint8_t* mask, const uint64_t index, const bool isFree)
{
    const uint64_t byteIndex = index / 8;
    const auto bitMask = static_cast<uint8_t>(1u << (index % 8));

    if (isFree) mask[byteIndex] |= bitMask;
    else mask[byteIndex] &= static_cast<uint8_t>(~bitMask);
}

bool BuddyAllocator::init()
{
    LockGuard guard(buddyLock);

    buddyBase = FrameAllocator::baseAddress();
    buddySize = FrameAllocator::size();

    if (buddyBase == 0 || buddySize < FrameAllocator::SMALL_SIZE)
    {
        Serial::printf("BuddyAllocator: No memory available for buddy allocator.\n");
        return false;
    }

    const uint64_t alignedBase = Alignment::alignUp(buddyBase, FrameAllocator::SMALL_SIZE),
                   alignedEnd = Alignment::alignDown(buddyBase + buddySize, FrameAllocator::SMALL_SIZE);
    if (alignedEnd <= alignedBase)
    {
        Serial::printf("BuddyAllocator: Region too small after alignment.\n");
        return false;
    }

    buddyBase = alignedBase;
    buddySize = alignedEnd - alignedBase;
    totalPages = buddySize / FrameAllocator::SMALL_SIZE;
    freePages = 0;

    int maxPossible = 0;
    while ((1ULL << (maxPossible + 1)) <= totalPages) ++maxPossible;
    maxOrder = MAX_WANTED_ORDER < maxPossible ? MAX_WANTED_ORDER : maxPossible;

    const uint64_t pagesBytes = totalPages * sizeof(Page),
                   listsBytes = static_cast<uint64_t>(maxOrder + 1) * sizeof(FreeList),
                   metaPages = (Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE) +
                           Alignment::alignUp(listsBytes, FrameAllocator::SMALL_SIZE) +
                           Alignment::alignUp((totalPages + 7) / 8, FrameAllocator::SMALL_SIZE)) /
                       FrameAllocator::SMALL_SIZE;
    if (metaPages >= totalPages)
    {
        Serial::printf("BuddyAllocator: Not enough space for metadata.\n");
        return false;
    }

    uint64_t metaPhys = 0;
    for (uint64_t i = 0; i < metaPages; ++i)
    {
        void* f = FrameAllocator::alloc();
        if (!f)
        {
            Serial::printf("BuddyAllocator: Failed to allocate frame for metadata.\n");
            return false;
        }

        if (i == 0) metaPhys = reinterpret_cast<uint64_t>(f);
        else if (reinterpret_cast<uint64_t>(f) != metaPhys + i * FrameAllocator::SMALL_SIZE)
        {
            Serial::printf("BuddyAllocator: Metadata frames are not contiguous.\n");
            return false;
        }
    }

    const uint64_t metaVirtBase = metaPhys + hhdm_request.response->offset;
    pages = reinterpret_cast<Page*>(metaVirtBase);
    freeLists = reinterpret_cast<FreeList*>(metaVirtBase + Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE));
    memset(pages, 0, totalPages * sizeof(Page));
    memset(freeLists, 0, (maxOrder + 1) * sizeof(FreeList));

    const auto isFree = reinterpret_cast<uint8_t*>(metaVirtBase +
        Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE) +
        Alignment::alignUp(listsBytes, FrameAllocator::SMALL_SIZE));
    memset(isFree, 0, (totalPages + 7) / 8);

    for (size_t i = 0; i < totalPages; ++i)
    {
        if (i < metaPages)
        {
            setFreeBit(isFree, i, false);
            continue;
        }
        setFreeBit(isFree, i, !FrameAllocator::used(reinterpret_cast<void*>(addressFromIndex(i))));
    }

    for (uint64_t i = 0; i < metaPages; ++i)
    {
        auto& page = pages[i];
        page.reserved = true;
        page.free = false;
        page.head = false;
        page.order = 0;
    }

    buildInitialFreeLists(isFree);
    return true;
}

uint64_t BuddyAllocator::alloc(const int order)
{
    if (order < 0 || order > maxOrder || !freePages || !freeLists) return 0;
    LockGuard guard(buddyLock);

    int currentOrder = order;
    while (currentOrder <= maxOrder && !freeLists[currentOrder].head) ++currentOrder;
    if (currentOrder > maxOrder) return 0;

    Page* block = freeLists[currentOrder].head;
    listRemove(currentOrder, block);

    const uint64_t index = block - pages;
    while (currentOrder > order)
    {
        --currentOrder;

        const uint64_t i = index + (1ULL << currentOrder);
        setBlockState(i, currentOrder, true, false);

        Page* buddy = &pages[i];
        buddy->order = static_cast<uint16_t>(currentOrder);
        buddy->head = true;
        buddy->free = true;
        buddy->reserved = false;

        listAdd(currentOrder, buddy);
        setBlockState(index, currentOrder, true, false);
    }

    setBlockState(index, order, false, false);
    Page* head = &pages[index];
    head->head = true;
    head->free = false;
    head->reserved = false;
    head->order = static_cast<uint16_t>(order);

    freePages -= 1ULL << order;
    return addressFromIndex(index);
}

void BuddyAllocator::free(const uint64_t address, const int order)
{
    if (!address || order < 0) return;
    LockGuard guard(buddyLock);

    if (!pages || !freeLists || address < buddyBase || address >= buddyBase + buddySize ||
        (address - buddyBase) % FrameAllocator::SMALL_SIZE != 0 || order > maxOrder)
        return;

    const uint64_t index = indexFromAddress(address);
    if (pages[index].reserved || pages[index].free || !pages[index].head ||
        pages[index].order != static_cast<uint16_t>(order))
        return;

    int currentOrder = order;
    uint64_t headIndex = index;

    while (currentOrder < maxOrder)
    {
        if ((headIndex & ((1ULL << currentOrder) - 1)) != 0) break;
        const uint64_t i = headIndex ^ (1ULL << currentOrder);
        if (i >= totalPages || (i & ((1ULL << currentOrder) - 1)) != 0) break;

        Page& buddy = pages[i];
        if (!buddy.free || !buddy.head || buddy.reserved || buddy.order != static_cast<uint16_t>(currentOrder)) break;

        listRemove(currentOrder, &buddy);
        headIndex = i < headIndex ? i : headIndex;
        ++currentOrder;
    }

    setBlockState(headIndex, currentOrder, true, false);
    Page* head = &pages[headIndex];
    head->order = static_cast<uint16_t>(currentOrder);
    head->head = true;
    head->free = true;
    head->reserved = false;

    listAdd(currentOrder, head);
    freePages += 1ULL << currentOrder;
}
