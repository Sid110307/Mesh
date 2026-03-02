#include <memory/paging.h>
#include <memory/smp.h>
#include <drivers/io/serial/serial.h>
#include <kernel/boot/limine.h>

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memmap_request;
extern limine_hhdm_request hhdm_request;
extern limine_executable_address_request executable_addr_request;
extern uint8_t _kernel_start[], _kernel_end[];

Spinlock Paging::pagingLock;
Spinlock FrameAllocator::frameAllocatorLock;

static uint64_t memoryBase = 0;
static uint64_t memorySize = 0;
static uint64_t totalFrames = 0;
static uint64_t usedFrames = 0;
static uint64_t* bitmap = nullptr;
uint64_t* pml4 = nullptr;

static uint64_t* createPageTable()
{
    void* frame = FrameAllocator::alloc();
    if (!frame) return nullptr;

    const auto address = reinterpret_cast<uint64_t*>(reinterpret_cast<uint64_t>(frame) + hhdm_request.response->offset);
    memset(address, 0, FrameAllocator::SMALL_SIZE);

    return address;
}

static uint64_t* ensureTable(uint64_t* parent, const uint16_t index, const PageFlags flags)
{
    if (parent[index] & static_cast<uint64_t>(PageFlags::HUGE))
    {
        Serial::printf("Paging: Cannot ensure table at index %u because parent entry is a huge page.\n", index);
        return nullptr;
    }
    const uint64_t want = static_cast<uint64_t>(PageFlags::PRESENT) | (static_cast<uint64_t>(flags) & (static_cast<
        uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER)));

    if (!(parent[index] & static_cast<uint64_t>(PageFlags::PRESENT)))
    {
        uint64_t* newTable = createPageTable();
        if (!newTable) return nullptr;

        const uint64_t phys = reinterpret_cast<uint64_t>(newTable) - hhdm_request.response->offset;
        parent[index] = phys | (static_cast<uint64_t>(PageFlags::PRESENT) | (static_cast<uint64_t>(flags) & (static_cast
            <uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER))));
    }
    else parent[index] |= want & (static_cast<uint64_t>(PageFlags::RW) | static_cast<uint64_t>(PageFlags::USER));

    return reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (parent[index] & ~0xFFFULL));
}

static bool tableEmpty(const uint64_t* table)
{
    for (int i = 0; i < 512; ++i) if (table[i] & static_cast<uint64_t>(PageFlags::PRESENT)) return false;
    return true;
}

static void freeTable(uint64_t* parent, const uint16_t index)
{
    FrameAllocator::free(reinterpret_cast<void*>(parent[index] & ~0xFFFULL));
    parent[index] = 0;
}

void Paging::init()
{
    pml4 = createPageTable();
    if (!pml4)
    {
        Serial::printf("Paging: Failed to create PML4 table.\n");
        while (true) asm volatile ("hlt");
    }

    uint64_t kernelPhysStart = executable_addr_request.response->physical_base;
    uint64_t kernelVirtStart = executable_addr_request.response->virtual_base;
    uint64_t kernelSize = _kernel_end - _kernel_start;

    for (uint64_t offset = 0; offset < kernelSize; offset += FrameAllocator::SMALL_SIZE)
    {
        uint64_t phys = kernelPhysStart + offset, virt = kernelVirtStart + offset;

        if (!mapSmall(phys, phys, PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL))
        {
            Serial::printf("Paging: Failed to map kernel page at 0x%lx to 0x%lx.\n", phys, virt);
            while (true) asm volatile ("hlt");
        }
        if (!mapSmall(virt, phys, PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL))
        {
            Serial::printf("Paging: Failed to map kernel page at 0x%lx to 0x%lx.\n", virt, phys);
            while (true) asm volatile ("hlt");
        }
    }

    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
    {
        auto* fb = framebuffer_request.response->framebuffers[0];
        auto fbBase = reinterpret_cast<uintptr_t>(fb->address);
        uint64_t fbSize = fb->pitch * fb->height;

        for (uint64_t addr = fbBase; addr < fbBase + fbSize; addr += FrameAllocator::SMALL_SIZE)
            if (!mapSmall(addr, addr, PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
            {
                Serial::printf("Paging: Failed to map framebuffer page at 0x%lx.\n", addr);
                while (true) asm volatile ("hlt");
            }
    }

    uint64_t maxPhys = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
        if (auto& entry = memmap_request.response->entries[i]; entry->type == LIMINE_MEMMAP_USABLE || entry->type ==
            LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE || entry->type ==
            LIMINE_MEMMAP_FRAMEBUFFER)
            if (uint64_t end = entry->base + entry->length; end > maxPhys) maxPhys = end;

    for (uint64_t phys = 0; phys < maxPhys; phys += FrameAllocator::SMALL_SIZE)
        if (!mapSmall(hhdm_request.response->offset + phys, phys,
                      PageFlags::PRESENT | PageFlags::RW | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
        {
            Serial::printf("Paging: Failed to map physical memory page at 0x%lx.\n",
                           phys + hhdm_request.response->offset);
            while (true) asm volatile ("hlt");
        }

    uint64_t pml4Phys = reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset;
    if (!mapSmall(reinterpret_cast<uint64_t>(pml4), pml4Phys, PageFlags::PRESENT | PageFlags::RW))
    {
        Serial::printf("Paging: Failed to map PML4 page at 0x%lx.\n", reinterpret_cast<uint64_t>(pml4));
        while (true) asm volatile ("hlt");
    }

    asm volatile ("mov %0, %%cr3" :: "r"(pml4Phys) : "memory");
    uint64_t cr4;
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1ULL << 7;
    asm volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
    uint32_t eax, edx;
    asm volatile ("mov $0xC0000080, %%ecx\nrdmsr\nor $(1 << 11), %%eax\nwrmsr\n" : "=a"(eax), "=d"(edx) :: "ecx",
        "memory");
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1 << 31 | 1 << 16;
    asm volatile ("mov %0, %%cr0" :: "r"(cr0));
}

bool Paging::mapSmall(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
    LockGuard guard(pagingLock);

    auto pml4Index = virtualAddress >> 39 & 0x1FF;
    auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    auto pdIndex = virtualAddress >> 21 & 0x1FF;
    auto ptIndex = virtualAddress >> 12 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;
    uint64_t* pd = ensureTable(pdpt, pdptIndex, flags);
    if (!pd) return false;
    uint64_t* pt = ensureTable(pd, pdIndex, flags);
    if (!pt) return false;

    pt[ptIndex] = (physicalAddress & ~0xFFFULL) | static_cast<uint64_t>(flags);
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

    return true;
}

bool Paging::mapMedium(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
    LockGuard guard(pagingLock);

    auto pml4Index = virtualAddress >> 39 & 0x1FF;
    auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    auto pdIndex = virtualAddress >> 21 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;
    uint64_t* pd = ensureTable(pdpt, pdptIndex, flags);
    if (!pd) return false;

    pd[pdIndex] = (physicalAddress & ~0x1FFFFFULL) | static_cast<uint64_t>(flags) | static_cast<uint64_t>(
        PageFlags::HUGE);
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

    return true;
}

bool Paging::mapLarge(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
    LockGuard guard(pagingLock);

    auto pml4Index = virtualAddress >> 39 & 0x1FF;
    auto pdptIndex = virtualAddress >> 30 & 0x1FF;

    uint64_t* pdpt = ensureTable(pml4, pml4Index, flags);
    if (!pdpt) return false;

    pdpt[pdptIndex] = (physicalAddress & ~0x3FFFFFFFULL) | static_cast<uint64_t>(flags) | static_cast<uint64_t>(
        PageFlags::HUGE);
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

    return true;
}

void Paging::unmapSmall(uint64_t virtualAddress)
{
    LockGuard guard(pagingLock);

    auto pml4Index = (virtualAddress >> 39) & 0x1FF;
    auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
    auto pdIndex = (virtualAddress >> 21) & 0x1FF;
    auto ptIndex = (virtualAddress >> 12) & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
    if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto* pt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pd[pdIndex] & ~0xFFFULL));
    if (!(pt[ptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pt[ptIndex] = 0;
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

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

void Paging::unmapMedium(uint64_t virtualAddress)
{
    LockGuard guard(pagingLock);

    auto pml4Index = virtualAddress >> 39 & 0x1FF;
    auto pdptIndex = virtualAddress >> 30 & 0x1FF;
    auto pdIndex = virtualAddress >> 21 & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
    if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pd[pdIndex] &= ~static_cast<uint64_t>(PageFlags::HUGE);
    pd[pdIndex] = 0;
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

    if (tableEmpty(pd))
    {
        freeTable(pdpt, static_cast<uint16_t>(pdptIndex));
        if (tableEmpty(pdpt)) freeTable(pml4, static_cast<uint16_t>(pml4Index));
    }
}

void Paging::unmapLarge(uint64_t virtualAddress)
{
    LockGuard guard(pagingLock);

    auto pml4Index = virtualAddress >> 39 & 0x1FF;
    auto pdptIndex = virtualAddress >> 30 & 0x1FF;

    if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
    auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
    if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

    pdpt[pdptIndex] &= ~static_cast<uint64_t>(PageFlags::HUGE);
    pdpt[pdptIndex] = 0;
    asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

    if (tableEmpty(pdpt)) freeTable(pml4, static_cast<uint16_t>(pml4Index));
}

void FrameAllocator::init()
{
    LockGuard guard(frameAllocatorLock);
    uint64_t bestBase = 0, bestSize = 0;

    for (size_t i = 0; i < memmap_request.response->entry_count; ++i)
    {
        const auto* entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE || entry->base < BIOS_START) continue;
        if (entry->length > bestSize)
        {
            bestBase = entry->base;
            bestSize = entry->length;
        }
    }

    if (bestBase == 0 || bestSize == 0)
    {
        Serial::printf("Paging: No memory region for frame allocator\n");
        while (true) asm volatile ("hlt");
    }

    uint64_t total = bestSize / SMALL_SIZE, bitmapBytes = (total + 63) / 64 * sizeof(uint64_t),
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
}

void* FrameAllocator::alloc()
{
    LockGuard guard(frameAllocatorLock);

    for (uint64_t i = 0; i < totalFrames; ++i)
        if (!(bitmap[i / 64] & 1ULL << (i % 64)))
        {
            bitmap[i / 64] |= 1ULL << (i % 64);
            ++usedFrames;

            return reinterpret_cast<void*>(memoryBase + i * SMALL_SIZE);
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
    bitmap[index / 64] &= ~(1ULL << (index % 64));
    --usedFrames;
}

void FrameAllocator::reserve(void* frame)
{
    const auto phys = reinterpret_cast<uint64_t>(frame);
    if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

    const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
    bitmap[index / 64] |= 1ULL << (index % 64);
    ++usedFrames;
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
