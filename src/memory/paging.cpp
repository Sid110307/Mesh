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
uint64_t memoryBase = 0, memorySize = 0, totalFrames = 0, usedFrames = 0, next = 0, *bitmap = nullptr, *pml4 = nullptr;
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

void reserve(void* frame)
{
    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % FrameAllocator::SMALL_SIZE != 0)
        return;

    const uint64_t index = (phys - memoryBase) / FrameAllocator::SMALL_SIZE;
    auto& word = bitmap[index / 64];

    if (const uint64_t mask = 1ULL << (index % 64); !(word & mask))
    {
        word |= mask;
        ++usedFrames;
    }
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
    uint64_t bestBase = 0, bestSize = 0;

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE || entry->base < 0x100000) continue;
        if (entry->length > bestSize)
        {
            bestBase = entry->base;
            bestSize = entry->length;
        }
    }

    if (bestBase == 0 || bestSize == 0)
    {
        Serial::printf("Paging: No memory region for frame allocator\n");
        return false;
    }

    const uint64_t total = bestSize / SMALL_SIZE, bitmapBytes = (total + 63) / 64 * sizeof(uint64_t),
                   bitmapPages = (bitmapBytes + SMALL_SIZE - 1) / SMALL_SIZE, bitmapPhys = bestBase;
    bitmap = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + bitmapPhys);
    memset(bitmap, 0, bitmapPages * SMALL_SIZE);

    memoryBase = bestBase + bitmapPages * SMALL_SIZE;
    memorySize = bestSize - bitmapPages * SMALL_SIZE;
    totalFrames = memorySize / SMALL_SIZE;
    usedFrames = 0;

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* e = memmap_request.response->entries[i];
        if (e->length == 0) continue;
        if (e->type == LIMINE_MEMMAP_USABLE) continue;

        uint64_t start = e->base, end = e->base + e->length;
        if (end <= memoryBase || start >= memoryBase + memorySize) continue;
        if (start < memoryBase) start = memoryBase;
        if (end > memoryBase + memorySize) end = memoryBase + memorySize;

        start = (start + SMALL_SIZE - 1) & ~(SMALL_SIZE - 1);
        end = end & ~(SMALL_SIZE - 1);

        for (uint64_t p = start; p < end; p += SMALL_SIZE) reserve(reinterpret_cast<void*>(p));
    }

    for (uint64_t i = 0; i < bitmapPages; ++i) reserve(reinterpret_cast<void*>(bitmapPhys + i * SMALL_SIZE));
    return true;
}

void* FrameAllocator::alloc()
{
    LockGuard guard(frameAllocatorLock);

    const uint64_t words = (totalFrames + 63) / 64;
    uint64_t w = next / 64;

    for (uint64_t pass = 0; pass < 2; ++pass)
    {
        for (; w < words; ++w)
        {
            const uint64_t used = bitmap[w];
            uint64_t freeMask = ~used;

            if (w == words - 1)
                if (const uint64_t validBits = totalFrames - (words - 1) * 64; validBits < 64)
                    freeMask &= (1ULL << validBits) - 1;
            if (!freeMask) continue;

            const uint64_t bit1 = __builtin_ctzll(freeMask) + 1;
            if (bit1 == 0) continue;
            bitmap[w] = used | (1ULL << (bit1 - 1));
            ++usedFrames;

            const uint64_t frameIndex = w * 64 + (bit1 - 1);
            next = frameIndex + 1;

            return reinterpret_cast<void*>(memoryBase + frameIndex * SMALL_SIZE);
        }
        w = 0;
    }

    Serial::printf("Paging: Out of memory in FrameAllocator! Used: %lu, Total: %lu\n", usedFrames, totalFrames);
    return nullptr;
}

void FrameAllocator::free(void* frame)
{
    LockGuard guard(frameAllocatorLock);

    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

    const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
    auto& word = bitmap[index / 64];

    if (const uint64_t mask = 1ULL << (index % 64); word & mask)
    {
        word &= ~mask;
        --usedFrames;
    }
}

bool FrameAllocator::used(void* frame)
{
    LockGuard guard(frameAllocatorLock);

    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return false;

    const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
    return bitmap[index / 64] & 1ULL << (index % 64);
}

uint64_t FrameAllocator::usedCount() { return usedFrames; }
uint64_t FrameAllocator::totalCount() { return totalFrames; }
uint64_t FrameAllocator::baseAddress() { return memoryBase; }
uint64_t FrameAllocator::size() { return memorySize; }
