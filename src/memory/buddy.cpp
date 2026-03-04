#include <core/limine.h>
#include <core/utils.h>
#include <drivers/serial.h>
#include <memory/buddy.h>
#include <memory/paging.h>
#include <memory/spinlock.h>

struct Page
{
    uint16_t order = 0;
    bool free = false, head = false, reserved = true;
    Page *next = nullptr, *prev = nullptr;
};

struct FreeList
{
    Page* head = nullptr;
};

extern limine_hhdm_request hhdm_request;

Spinlock buddyLock;
uint64_t buddyBase = 0, buddySize = 0, totalPages = 0, freePages = 0;
int maxOrder = 0;
Page* pages = nullptr;
FreeList* freeLists = nullptr;

uint64_t indexFromAddress(const uint64_t address) { return (address - buddyBase) / FrameAllocator::SMALL_SIZE; }
uint64_t addressFromIndex(const uint64_t index) { return buddyBase + index * FrameAllocator::SMALL_SIZE; }
Page* buddyPageFromAddress(const uint64_t address) { return &pages[indexFromAddress(address)]; }
uint64_t indexFromPage(const Page* page) { return page - pages; }
uint64_t buddyIndex(const uint64_t index, const uint16_t order) { return index ^ (1ULL << order); }
bool inRange(const uint64_t address) { return address >= buddyBase && address < buddyBase + buddySize; }

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
    uint64_t i = 0;
    while (i < totalPages)
    {
        if (!(freeMask[i / 8] & (1 << (i % 8))))
        {
            ++i;
            continue;
        }

        int best = 0;
        for (int order = maxOrder; order >= 0; --order)
        {
            const uint64_t blockPages = 1ULL << order;
            if (((i & (blockPages - 1)) != 0) || (i + blockPages > totalPages)) continue;

            bool ok = true;
            for (uint64_t j = 0; j < blockPages; ++j)
                if (!(freeMask[(i + j) / 8] & (1 << ((i + j) % 8))))
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

    int maxWanted = 9, maxPossible = 0;
    while ((1ULL << (maxPossible + 1)) <= totalPages) ++maxPossible;
    maxOrder = maxWanted < maxPossible ? maxWanted : maxPossible;

    const uint64_t pagesBytes = totalPages * sizeof(Page),
                   listsBytes = static_cast<uint64_t>(maxOrder + 1) * sizeof(FreeList),
                   totalBytes = Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE) +
                       Alignment::alignUp(listsBytes, FrameAllocator::SMALL_SIZE),
                   metaPages = totalBytes / FrameAllocator::SMALL_SIZE;

    if (metaPages >= totalPages)
    {
        Serial::printf("BuddyAllocator: Not enough space for metadata.\n");
        return false;
    }

    const auto metaPhysStart = reinterpret_cast<uint64_t>(FrameAllocator::alloc());
    if (!metaPhysStart)
    {
        Serial::printf("BuddyAllocator: Failed to allocate metadata frame.\n");
        return false;
    }
    FrameAllocator::free(reinterpret_cast<void*>(metaPhysStart));

    const uint64_t metaPhys = buddyBase, metaVirtBase = metaPhys + hhdm_request.response->offset,
                   metaSizeBytes = metaPages * FrameAllocator::SMALL_SIZE;
    if (metaSizeBytes + FrameAllocator::SMALL_SIZE > buddySize)
    {
        Serial::printf("BuddyAllocator: Not enough space for metadata.\n");
        return false;
    }

    pages = reinterpret_cast<Page*>(metaVirtBase);
    freeLists = reinterpret_cast<FreeList*>(metaVirtBase + Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE));
    memset(pages, 0, totalPages * sizeof(Page));
    memset(freeLists, 0, (maxOrder + 1) * sizeof(FreeList));

    auto isFree = reinterpret_cast<uint8_t*>(metaVirtBase + Alignment::alignUp(pagesBytes, FrameAllocator::SMALL_SIZE) +
        Alignment::alignUp(listsBytes, FrameAllocator::SMALL_SIZE));
    const uint64_t maskBytes = totalPages, maskEnd = reinterpret_cast<uint64_t>(isFree) + maskBytes - metaVirtBase;
    if (maskEnd > metaSizeBytes)
    {
        Serial::printf("BuddyAllocator: Not enough space for free mask.\n");
        return false;
    }

    memset(isFree, 0, maskBytes);
    for (uint64_t i = 0; i < totalPages; ++i)
    {
        if (i < metaPages)
        {
            isFree[i] = 0;
            continue;
        }

        const uint64_t phys = addressFromIndex(i);
        if (FrameAllocator::used(reinterpret_cast<void*>(phys)))
        {
            isFree[i] = 0;
            continue;
        }

        isFree[i] = 1;
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

    const uint64_t index = indexFromPage(block);
    while (currentOrder > order)
    {
        --currentOrder;

        const uint64_t i = indexFromPage(block) + (1ULL << currentOrder);
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

    if (!pages || !freeLists) return;
    if (!inRange(address)) return;
    if ((address - buddyBase) % FrameAllocator::SMALL_SIZE != 0) return;
    if (order > maxOrder) return;

    const uint64_t index = indexFromAddress(address);
    const Page& page = pages[index];
    if (page.reserved || page.free || !page.head || page.order != static_cast<uint16_t>(order)) return;

    int currentOrder = order;
    uint64_t headIndex = index;

    while (currentOrder < maxOrder)
    {
        const uint64_t i = buddyIndex(headIndex, currentOrder);
        if (i >= totalPages) break;

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
    freePages += 1ULL << order;
}

void BuddyAllocator::reserve(const uint64_t address, const uint64_t size)
{
    LockGuard guard(buddyLock);

    if (!pages || !freeLists || size == 0) return;
    const uint64_t start = Alignment::alignDown(address, FrameAllocator::SMALL_SIZE),
                   end = Alignment::alignUp(address + size, FrameAllocator::SMALL_SIZE);

    if (end <= start) return;
    for (uint64_t addr = start; addr < end; addr += FrameAllocator::SMALL_SIZE) if (inRange(addr)) free(addr, 0);
}
