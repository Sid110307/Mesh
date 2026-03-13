#include <core/limine.h>
#include <core/panic.h>
#include <drivers/serial.h>
#include <memory/paging.h>

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memmap_request;
extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern uint8_t _text_start[], _text_end[], _rodata_start[], _rodata_end[], __data_start[], __data_end[], __bss_start[],
               __bss_end[];

Spinlock pagingLock, frameAllocatorLock;
uint64_t bitmapSize = 0, bitmapPhysStart = 0, freePages = 0, totalPages = 0, *bitmap = nullptr, *pml4 = nullptr;
bool pagingInitialized = false;

void invlpg(const uint64_t address) { if (pagingInitialized) asm volatile ("invlpg (%0)" :: "r"(address) : "memory"); }

uint64_t* createPageTable()
{
    void* frame = FrameAllocator::alloc();
    if (!frame) return nullptr;

    const auto address = reinterpret_cast<uint64_t*>(reinterpret_cast<uint64_t>(frame) + hhdm_request.response->offset);
    memset(address, 0, FrameAllocator::SMALL_SIZE);

    return address;
}

uint64_t* ensureTable(uint64_t* parent, const uint16_t index, const PageFlags flags)
{
    if (parent[index] & static_cast<uint64_t>(PageFlags::HUGE))
    {
        Serial::printf("Paging: Cannot ensure table at index %u because parent entry is a huge page\n", index);
        return nullptr;
    }
    const uint64_t want = static_cast<uint64_t>(PageFlags::PRESENT) | (static_cast<uint64_t>(flags) &
        (static_cast<uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER)));

    if (!(parent[index] & static_cast<uint64_t>(PageFlags::PRESENT)))
    {
        uint64_t* newTable = createPageTable();
        if (!newTable) return nullptr;

        const uint64_t phys = reinterpret_cast<uint64_t>(newTable) - hhdm_request.response->offset;
        parent[index] = phys | (static_cast<uint64_t>(PageFlags::PRESENT) | (static_cast<uint64_t>(flags) &
            (static_cast<uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER))));
    }
    else parent[index] |= want & (static_cast<uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER));

    return reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (parent[index] & ~0xFFFULL));
}

bool tableEmpty(const uint64_t* table)
{
    for (int i = 0; i < 512; ++i) if (table[i] & static_cast<uint64_t>(PageFlags::PRESENT)) return false;
    return true;
}

void freeTable(uint64_t* parent, const uint16_t index)
{
    FrameAllocator::free(reinterpret_cast<void*>(parent[index] & ~0xFFFULL));
    parent[index] = 0;
}

bool Alignment::overlaps(const uint64_t address1, const uint64_t size1, const uint64_t address2, const uint64_t size2)
{
    return address1 < address2 + size2 && address2 < address1 + size1;
}

bool Alignment::aligned(const uint64_t address, const uint64_t size) { return (address & (size - 1)) == 0; }
uint64_t Alignment::alignDown(const uint64_t address, const uint64_t size) { return address & ~(size - 1); }
uint64_t Alignment::alignUp(const uint64_t address, const uint64_t size) { return (address + size - 1) & ~(size - 1); }

bool mapSmall(const uint64_t virtualAddress, const uint64_t physicalAddress, PageFlags flags)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    const auto pdIndex = virtualAddress >> 21 & 0x1FF;
    const auto ptIndex = virtualAddress >> 12 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;
    uint64_t* pd = ensureTable(pdpt, pdptIndex, flags);
    if (!pd) return false;
    uint64_t* pt = ensureTable(pd, pdIndex, flags);
    if (!pt) return false;

    pt[ptIndex] = (physicalAddress & ~0xFFFULL) | static_cast<uint64_t>(flags);
    invlpg(virtualAddress);

    return true;
}

bool mapMedium(const uint64_t virtualAddress, const uint64_t physicalAddress, PageFlags flags)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    const auto pdIndex = virtualAddress >> 21 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;
    uint64_t* pd = ensureTable(pdpt, pdptIndex, flags);
    if (!pd) return false;

    pd[pdIndex] = (physicalAddress & ~0x1FFFFFULL) | static_cast<uint64_t>(flags) | static_cast<uint64_t>(
        PageFlags::HUGE);
    invlpg(virtualAddress);

    return true;
}

bool mapLarge(const uint64_t virtualAddress, const uint64_t physicalAddress, PageFlags flags)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;

    pdpt[pdptIndex] = (physicalAddress & ~0x3FFFFFFFULL) | static_cast<uint64_t>(flags) | static_cast<uint64_t>(
        PageFlags::HUGE);
    invlpg(virtualAddress);

    return true;
}

void unmapSmall(const uint64_t virtualAddress)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    const auto pdIndex = virtualAddress >> 21 & 0x1FF;
    const auto ptIndex = virtualAddress >> 12 & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
    if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pd[pdIndex] & ~0xFFFULL));
    if (!(pt[ptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pt[ptIndex] = 0;
    invlpg(virtualAddress);

    if (tableEmpty(pt))
    {
        freeTable(pd, static_cast<uint16_t>(pdIndex));
        if (tableEmpty(pd))
        {
            freeTable(pdpt, static_cast<uint16_t>(pdptIndex));
            if (tableEmpty(pdpt)) freeTable(pml4, static_cast<uint16_t>(pml4Index));
        }
    }
}

void unmapMedium(const uint64_t virtualAddress)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    const auto pdIndex = virtualAddress >> 21 & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    const auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    const auto pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
    if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pd[pdIndex] = 0;
    invlpg(virtualAddress);

    if (tableEmpty(pd))
    {
        freeTable(pdpt, static_cast<uint16_t>(pdptIndex));
        if (tableEmpty(pdpt)) freeTable(pml4, static_cast<uint16_t>(pml4Index));
    }
}

void unmapLarge(const uint64_t virtualAddress)
{
    const auto pml4Index = virtualAddress >> 39 & 0x1FF;
    const auto pdptIndex = virtualAddress >> 30 & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    const auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pdpt[pdptIndex] = 0;
    invlpg(virtualAddress);

    if (tableEmpty(pdpt)) freeTable(pml4, static_cast<uint16_t>(pml4Index));
}

bool Paging::init()
{
    pml4 = createPageTable();
    if (!pml4)
    {
        Serial::printf("Paging: Failed to create PML4 table\n");
        return false;
    }

    const uint64_t kernelDelta = executable_addr_request.response->virtual_base - executable_addr_request.response->
        physical_base;

    if (const auto textVirt = reinterpret_cast<uint64_t>(_text_start);
        !map(textVirt, textVirt - kernelDelta, reinterpret_cast<uint64_t>(_text_end) - textVirt,
             PageFlags::PRESENT | PageFlags::GLOBAL))
    {
        Serial::printf("Paging: Failed to map text page at 0x%lx to 0x%lx\n", textVirt, textVirt - kernelDelta);
        return false;
    }

    if (const auto rodataVirt = reinterpret_cast<uint64_t>(_rodata_start);
        !map(rodataVirt, rodataVirt - kernelDelta, reinterpret_cast<uint64_t>(_rodata_end) - rodataVirt,
             PageFlags::PRESENT | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Serial::printf("Paging: Failed to map rodata page at 0x%lx to 0x%lx\n", rodataVirt, rodataVirt - kernelDelta);
        return false;
    }

    if (const auto dataVirt = reinterpret_cast<uint64_t>(__data_start);
        !map(dataVirt, dataVirt - kernelDelta, reinterpret_cast<uint64_t>(__data_end) - dataVirt,
             PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Serial::printf("Paging: Failed to map data page at 0x%lx to 0x%lx\n", dataVirt, dataVirt - kernelDelta);
        return false;
    }

    if (const auto bssVirt = reinterpret_cast<uint64_t>(__bss_start);
        !map(bssVirt, bssVirt - kernelDelta, reinterpret_cast<uint64_t>(__bss_end) - bssVirt,
             PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
    {
        Serial::printf("Paging: Failed to map bss page at 0x%lx to 0x%lx\n", bssVirt, bssVirt - kernelDelta);
        return false;
    }

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* e = memmap_request.response->entries[i];
        if (!e || e->length == 0 || e->type == LIMINE_MEMMAP_BAD_MEMORY || e->type == LIMINE_MEMMAP_RESERVED) continue;

        if (!map(e->base + hhdm_request.response->offset, e->base, e->length,
                 PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
            Panic::panic("Failed to map physical memory page at 0x%lx\n", e->base + hhdm_request.response->offset);
    }

    asm volatile ("mov %0, %%cr3" :: "r"(reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset) : "memory");
    uint64_t cr4;
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1ULL << 7;
    asm volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080));
    low |= 1u << 11;
    asm volatile ("wrmsr" :: "a"(low), "d"(high), "c"(0xC0000080));
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1 << 31 | 1 << 16;
    asm volatile ("mov %0, %%cr0" :: "r"(cr0));

    pagingInitialized = true;
    return true;
}

bool Paging::map(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t size, const PageFlags flags)
{
    if (size == 0) return false;
    LockGuard guard(pagingLock);

    virtualAddress = Alignment::alignDown(virtualAddress, FrameAllocator::SMALL_SIZE);
    physicalAddress = Alignment::alignDown(physicalAddress, FrameAllocator::SMALL_SIZE);
    size = Alignment::alignUp(virtualAddress + size, FrameAllocator::SMALL_SIZE) - virtualAddress;

    while (size)
    {
        if (size >= FrameAllocator::LARGE_SIZE && Alignment::aligned(virtualAddress, FrameAllocator::LARGE_SIZE) &&
            Alignment::aligned(physicalAddress, FrameAllocator::LARGE_SIZE))
        {
            if (!mapLarge(virtualAddress, physicalAddress, flags)) return false;
            virtualAddress += FrameAllocator::LARGE_SIZE;
            physicalAddress += FrameAllocator::LARGE_SIZE;
            size -= FrameAllocator::LARGE_SIZE;

            continue;
        }

        if (size >= FrameAllocator::MEDIUM_SIZE && Alignment::aligned(virtualAddress, FrameAllocator::MEDIUM_SIZE) &&
            Alignment::aligned(physicalAddress, FrameAllocator::MEDIUM_SIZE))
        {
            if (!mapMedium(virtualAddress, physicalAddress, flags)) return false;
            virtualAddress += FrameAllocator::MEDIUM_SIZE;
            physicalAddress += FrameAllocator::MEDIUM_SIZE;
            size -= FrameAllocator::MEDIUM_SIZE;

            continue;
        }

        if (!mapSmall(virtualAddress, physicalAddress, flags)) return false;
        virtualAddress += FrameAllocator::SMALL_SIZE;
        physicalAddress += FrameAllocator::SMALL_SIZE;
        size -= FrameAllocator::SMALL_SIZE;
    }

    return true;
}

void Paging::unmap(const uint64_t virtualAddress, const uint64_t size)
{
    if (size == 0) return;
    LockGuard guard(pagingLock);

    uint64_t start = Alignment::alignDown(virtualAddress, FrameAllocator::SMALL_SIZE);
    const uint64_t end = Alignment::alignUp(virtualAddress + size, FrameAllocator::SMALL_SIZE);

    while (start < end)
    {
        if (start + FrameAllocator::LARGE_SIZE <= end && Alignment::aligned(start, FrameAllocator::LARGE_SIZE))
        {
            unmapLarge(start);
            start += FrameAllocator::LARGE_SIZE;
        }
        else if (start + FrameAllocator::MEDIUM_SIZE <= end && Alignment::aligned(start, FrameAllocator::MEDIUM_SIZE))
        {
            unmapMedium(start);
            start += FrameAllocator::MEDIUM_SIZE;
        }
        else
        {
            unmapSmall(start);
            start += FrameAllocator::SMALL_SIZE;
        }
    }
}

bool FrameAllocator::init()
{
    LockGuard guard(frameAllocatorLock);

    uint64_t highestAddr = 0;
    totalPages = 0;

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;
        const uint64_t start = Alignment::alignUp(entry->base, SMALL_SIZE),
                       end = Alignment::alignDown(entry->base + entry->length, SMALL_SIZE);

        if (end <= start) continue;
        if (end > highestAddr) highestAddr = end;

        totalPages += (end - start) / SMALL_SIZE;
    }

    if (highestAddr == 0 || totalPages == 0)
    {
        Serial::printf("Paging: No usable memory regions for frame allocator\n");
        return false;
    }

    const uint64_t bitmapBits = highestAddr / SMALL_SIZE,
                   bitmapBytes = (bitmapBits + 63) / 64 * sizeof(uint64_t),
                   bitmapPages = (bitmapBytes + SMALL_SIZE - 1) / SMALL_SIZE;
    uint8_t* bitmapVirt = nullptr;

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;
        if (entry->length < bitmapPages * SMALL_SIZE) continue;

        bitmapVirt = reinterpret_cast<uint8_t*>(entry->base + hhdm_request.response->offset);
        bitmapPhysStart = entry->base;

        break;
    }

    if (!bitmapVirt)
    {
        Serial::printf("Paging: No suitable memory region for frame allocator bitmap\n");
        return false;
    }

    bitmap = reinterpret_cast<uint64_t*>(bitmapVirt);
    bitmapSize = bitmapBits;
    freePages = 0;
    memset(bitmap, 0xFF, bitmapBytes);

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* e = memmap_request.response->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        const uint64_t start = Alignment::alignUp(e->base, SMALL_SIZE);
        const uint64_t end = Alignment::alignDown(e->base + e->length, SMALL_SIZE);
        if (end <= start) continue;

        for (uint64_t addr = start; addr < end; addr += SMALL_SIZE)
        {
            const uint64_t index = addr / SMALL_SIZE;
            bitmap[index / 64] &= ~(1ULL << (index % 64));
        }
        freePages += (end - start) / SMALL_SIZE;
    }

    for (uint64_t p = 0; p < bitmapPages; ++p)
    {
        const uint64_t index = bitmapPhysStart / SMALL_SIZE + p;
        if (const uint64_t mask = 1ULL << (index % 64); !(bitmap[index / 64] & mask))
        {
            bitmap[index / 64] |= mask;
            --freePages;
        }
    }

    if (!(bitmap[0] & 1ULL))
    {
        bitmap[0] |= 1ULL;
        --freePages;
    }
    return true;
}

void* FrameAllocator::alloc()
{
    LockGuard guard(frameAllocatorLock);

    const uint64_t words = (bitmapSize + 63) / 64;
    for (uint64_t word = 0; word < words; ++word)
    {
        uint64_t freeMask = ~bitmap[word];

        if (word == words - 1)
            if (const uint64_t validBits = bitmapSize - word * 64; validBits < 64) freeMask &= (1ULL << validBits) - 1;
        if (!freeMask) continue;

        const uint64_t bit = __builtin_ctzll(freeMask);
        bitmap[word] |= 1ULL << bit;
        --freePages;

        return reinterpret_cast<void*>((word * 64 + bit) * SMALL_SIZE);
    }

    Serial::printf("Paging: Out of memory! (free: %lu pages, total: %lu pages)\n", freePages, totalPages);
    return nullptr;
}

void FrameAllocator::free(void* frame)
{
    LockGuard guard(frameAllocatorLock);

    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys % SMALL_SIZE != 0) return;
    const uint64_t index = phys / SMALL_SIZE;
    if (index >= bitmapSize) return;

    if (const uint64_t mask = 1ULL << (index % 64); bitmap[index / 64] & mask)
    {
        bitmap[index / 64] &= ~mask;
        ++freePages;
    }
}

bool FrameAllocator::used(void* frame)
{
    LockGuard guard(frameAllocatorLock);

    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys % SMALL_SIZE != 0) return false;
    const uint64_t index = phys / SMALL_SIZE;
    if (index >= bitmapSize) return false;

    return bitmap[index / 64] & (1ULL << (index % 64));
}

uint64_t FrameAllocator::usedCount() { return totalPages - freePages; }
uint64_t FrameAllocator::totalCount() { return totalPages; }
uint64_t FrameAllocator::baseAddress() { return 0; }
uint64_t FrameAllocator::size() { return bitmapSize * SMALL_SIZE; }
