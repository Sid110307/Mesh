#include "./paging.h"
#include "./smp.h"
#include "../drivers/io/serial/serial.h"
#include "../arch/common/spinlock.h"
#include "../boot/limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memory_request;
extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;

extern uint8_t _kernel_start[], _kernel_end[];
static Spinlock pagingLock, frameAllocatorLock;

static uint64_t memoryBase = 0;
static uint64_t memorySize = 0;
static uint64_t totalFrames = 0;
static uint64_t usedFrames = 0;
static uint64_t* bitmap = nullptr;
static uint64_t* pml4 = nullptr;

static uint64_t* createPageTable()
{
	void* frame = FrameAllocator::alloc();
	if (!frame) return nullptr;

	const auto address = reinterpret_cast<uint64_t*>(reinterpret_cast<uint64_t>(frame) + hhdm_request.response->offset);
	memset(address, 0, FrameAllocator::SMALL_SIZE);

	return address;
}

static uint64_t* ensureTable(uint64_t* parent, const uint16_t index,
                             const PageFlags flags = PageFlags::PRESENT | PageFlags::RW)
{
	if (!(parent[index] & static_cast<uint64_t>(PageFlags::PRESENT)))
	{
		uint64_t* newTable = createPageTable();
		if (!newTable) return nullptr;

		const uint64_t phys = reinterpret_cast<uint64_t>(newTable) - hhdm_request.response->offset;
		parent[index] = phys | static_cast<uint64_t>(flags);
	}
	return reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (parent[index] & ~0xFFFULL));
}

void Paging::init()
{
	pml4 = createPageTable();
	if (!pml4)
	{
		Serial::printf("Failed to create PML4 table.\n");
		while (true) asm volatile("hlt");
	}

	uint64_t kernelPhysStart = kernel_addr_request.response->physical_base;
	uint64_t kernelVirtStart = kernel_addr_request.response->virtual_base;
	uint64_t kernelSize = _kernel_end - _kernel_start;

	for (uint64_t offset = 0; offset < kernelSize; offset += FrameAllocator::SMALL_SIZE)
	{
		uint64_t phys = kernelPhysStart + offset;
		uint64_t virt = kernelVirtStart + offset;

		if (!mapSmall(phys, phys, PageFlags::PRESENT | PageFlags::RW))
		{
			Serial::printf("Failed to map kernel page at 0x%lx to 0x%lx.\n", phys, virt);
			while (true) asm volatile("hlt");
		}
		if (!mapSmall(virt, phys, PageFlags::PRESENT | PageFlags::RW))
		{
			Serial::printf("Failed to map kernel page at 0x%lx to 0x%lx.\n", virt, phys);
			while (true) asm volatile("hlt");
		}
	}

	if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
	{
		auto* fb = framebuffer_request.response->framebuffers[0];
		auto fbBase = reinterpret_cast<uintptr_t>(fb->address);
		uint64_t fbSize = fb->pitch * fb->height;

		for (uint64_t addr = fbBase; addr < fbBase + fbSize; addr += FrameAllocator::SMALL_SIZE)
			if (!mapSmall(addr, addr, PageFlags::PRESENT | PageFlags::RW))
			{
				Serial::printf("Failed to map framebuffer page at 0x%lx.\n", addr);
				while (true) asm volatile("hlt");
			}
	}

	uint64_t maxPhys = 0;
	for (size_t i = 0; i < memory_request.response->entry_count; ++i)
		if (auto& entry = memory_request.response->entries[i]; entry->type == LIMINE_MEMMAP_USABLE || entry->type ==
			LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE || entry->type ==
			LIMINE_MEMMAP_FRAMEBUFFER)
			if (uint64_t end = entry->base + entry->length; end > maxPhys) maxPhys = end;

	for (uint64_t phys = 0; phys < maxPhys; phys += FrameAllocator::SMALL_SIZE)
		if (!mapSmall(hhdm_request.response->offset + phys, phys, PageFlags::PRESENT | PageFlags::RW))
		{
			Serial::printf("Failed to map HHDM page at 0x%lx.\n", hhdm_request.response->offset + phys);
			while (true) asm volatile("hlt");
		}

	if (!mapSmall(SMP::LAPIC_BASE, SMP::LAPIC_BASE, PageFlags::PRESENT | PageFlags::RW))
	{
		Serial::printf("Failed to map LAPIC page at 0x%lx.\n", SMP::LAPIC_BASE);
		while (true) asm volatile("hlt");
	}

	uint64_t pml4Phys = reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset;

	asm volatile("cli");
	asm volatile ("mov %0, %%cr3" :: "r"(pml4Phys) : "memory");
	uint64_t cr4;
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= 1 << 5;
	asm volatile ("mov %0, %%cr4" :: "r"(cr4));
	uint32_t eax, edx;
	asm volatile ("mov $0xC0000080, %%ecx\nrdmsr\nor $(1 << 8), %%eax\nwrmsr\n" : "=a"(eax), "=d"(edx) :: "ecx");
	uint64_t cr0;
	asm volatile ("mov %%cr0, %0" : "=r"(cr0));
	cr0 |= 1 << 31 | 1 << 16;
	asm volatile ("mov %0, %%cr0" :: "r"(cr0));
}

bool Paging::mapSmall(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
	LockGuard guard(pagingLock);

	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
	auto pdIndex = (virtualAddress >> 21) & 0x1FF;
	auto ptIndex = (virtualAddress >> 12) & 0x1FF;

	uint64_t* pdpt = ensureTable(pml4, pml4Index);
	if (!pdpt) return false;
	uint64_t* pd = ensureTable(pdpt, pdptIndex);
	if (!pd) return false;
	uint64_t* pt = ensureTable(pd, pdIndex);
	if (!pt) return false;

	pt[ptIndex] = (physicalAddress & ~0xFFFULL) | static_cast<uint64_t>(flags);
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

	return true;
}

bool Paging::mapMedium(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
	LockGuard guard(pagingLock);

	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
	auto pdIndex = (virtualAddress >> 21) & 0x1FF;

	uint64_t* pdpt = ensureTable(pml4, pml4Index);
	if (!pdpt) return false;
	uint64_t* pd = ensureTable(pdpt, pdptIndex);
	if (!pd) return false;

	pd[pdIndex] = (physicalAddress & ~0x1FFFFFULL) | static_cast<uint64_t>(flags) | static_cast<uint64_t>(
		PageFlags::HUGE);
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

	return true;
}

bool Paging::mapLarge(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags)
{
	LockGuard guard(pagingLock);

	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;

	uint64_t* pdpt = ensureTable(pml4, pml4Index);
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
	auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
	if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
	if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pd[pdIndex] & ~0xFFFULL));
	if (!(pt[ptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

	pt[ptIndex] = 0;
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");
	cleanup(pt, ptIndex, 1, pdptIndex, pdIndex, pd, pdpt);
}

void Paging::unmapMedium(uint64_t virtualAddress)
{
	LockGuard guard(pagingLock);

	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
	auto pdIndex = (virtualAddress >> 21) & 0x1FF;

	if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
	if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
	if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

	pd[pdIndex] &= ~static_cast<uint64_t>(PageFlags::HUGE);
	pd[pdIndex] = 0;
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");
	cleanup(pd, pdIndex, 2, pdptIndex, pdIndex, pd, pdpt);
}

void Paging::unmapLarge(uint64_t virtualAddress)
{
	LockGuard guard(pagingLock);

	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;

	if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
	if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

	pdpt[pdptIndex] &= ~static_cast<uint64_t>(PageFlags::HUGE);
	pdpt[pdptIndex] = 0;
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");
	cleanup(pdpt, pdptIndex, 3, pml4Index, pdptIndex, nullptr, nullptr);
}

void Paging::cleanup(uint64_t* startTable, const uint16_t startIndex, const int startLevel,
                     const uint64_t pdptIndex, const uint64_t pdIndex, const uint64_t* pd, const uint64_t* pdpt)
{
	uint64_t* currentTable = startTable;
	uint16_t currentIndex = startIndex;
	int currentLevel = startLevel;

	while (true)
	{
		if (const bool cleaned = cleanupPageTable(currentTable, currentIndex, currentLevel); !cleaned) break;
		if (currentLevel == 1)
		{
			currentTable = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pd[currentIndex] & ~0xFFFULL));
			currentIndex = pdIndex;
			currentLevel = 2;
		}
		else if (currentLevel == 2)
		{
			currentTable = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[currentIndex] & ~
				0xFFFULL));
			currentIndex = pdptIndex;
			currentLevel = 3;
		}
		else if (currentLevel == 3)
		{
			if (pml4[currentIndex] != 0) pml4[currentIndex] = 0;
			break;
		}
		else break;
	}
}

bool Paging::cleanupPageTable(uint64_t* rootTable, uint16_t rootIndex, int rootLevel)
{
	struct CleanupState
	{
		uint64_t* parent;
		uint16_t index;
		uint64_t* table;
		int level;
	};

	CleanupState stack[4];
	int stackPtr = 0;
	stack[stackPtr++] = {rootTable, rootIndex, nullptr, rootLevel};

	while (stackPtr > 0)
	{
		auto& [parent, index, table, level] = stack[stackPtr - 1];

		if (!table)
		{
			table = parent && level >= 2 && level <= 4
				        ? reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (parent[index] & ~0xFFFULL))
				        : nullptr;
			if (!table)
			{
				stackPtr--;
				continue;
			}
		}

		bool empty = true;
		for (int i = 0; i < 512; ++i)
			if (table[i] & static_cast<uint64_t>(PageFlags::PRESENT))
			{
				empty = false;
				break;
			}

		if (!empty)
		{
			stackPtr--;
			continue;
		}

		FrameAllocator::free(table);
		if (parent) parent[index] = 0;

		uint64_t virtBase = 0;
		switch (level)
		{
			case 1:
				break;
			case 2:
				virtBase = index * 0x200000ULL;
				break;
			case 3:
				virtBase = index * 0x40000000ULL;
				break;
			case 4:
				virtBase = index * 0x8000000000ULL;
				break;
			default:
				return false;
		}
		if (virtBase != 0) asm volatile ("invlpg (%0)" :: "r"(virtBase) : "memory");

		stackPtr--;
		if (parent && level < 4) break;
	}

	return true;
}

void FrameAllocator::init()
{
	LockGuard guard(frameAllocatorLock);
	uint64_t bestBase = 0, bestSize = 0;

	for (size_t i = 0; i < memory_request.response->entry_count; ++i)
	{
		const auto* entry = memory_request.response->entries[i];
		if (entry->type != LIMINE_MEMMAP_USABLE || entry->base < BIOS_START) continue;
		if (entry->length > bestSize)
		{
			bestBase = entry->base;
			bestSize = entry->length;
		}
	}

	if (bestBase == 0 || bestSize == 0)
	{
		Serial::printf("No memory region for frame allocator\n");
		while (true) asm volatile("hlt");
	}

	uint64_t total = bestSize / SMALL_SIZE, bitmapBytes = ((total + 63) / 64) * sizeof(uint64_t),
	         bitmapPages = (bitmapBytes + SMALL_SIZE - 1) / SMALL_SIZE, bitmapPhys = bestBase;
	bitmap = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + bitmapPhys);
	memset(bitmap, 0, bitmapPages * SMALL_SIZE);

	memoryBase = bestBase + bitmapPages * SMALL_SIZE;
	memorySize = bestSize - bitmapPages * SMALL_SIZE;
	totalFrames = memorySize / SMALL_SIZE;
	usedFrames = 0;

	for (uint64_t i = 0; i < bitmapPages; ++i) reserve(reinterpret_cast<void*>(bitmapPhys + i * SMALL_SIZE));
}

void* FrameAllocator::alloc()
{
	LockGuard guard(frameAllocatorLock);

	for (uint64_t i = 0; i < totalFrames; ++i)
		if (!(bitmap[i / 64] & (1ULL << (i % 64))))
		{
			bitmap[i / 64] |= 1ULL << (i % 64);
			++usedFrames;

			return reinterpret_cast<void*>(memoryBase + i * SMALL_SIZE);
		}
	return nullptr;
}

void FrameAllocator::free(void* frame)
{
	LockGuard guard(frameAllocatorLock);

	const auto addr = reinterpret_cast<uint64_t>(frame);
	const uint64_t phys = addr - hhdm_request.response->offset;
	if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	bitmap[index / 64] &= ~(1ULL << (index % 64));
	--usedFrames;
}

void FrameAllocator::reserve(void* frame)
{
	const auto addr = reinterpret_cast<uint64_t>(frame);
	const uint64_t phys = addr - hhdm_request.response->offset;
	if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	bitmap[index / 64] |= 1ULL << (index % 64);
	++usedFrames;
}

bool FrameAllocator::used(void* frame)
{
	LockGuard guard(frameAllocatorLock);

	const auto addr = reinterpret_cast<uint64_t>(frame);
	const uint64_t phys = addr - hhdm_request.response->offset;
	if (phys < memoryBase || phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return false;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	return bitmap[index / 64] & (1ULL << (index % 64));
}

uint64_t FrameAllocator::usedCount() { return usedFrames; }
uint64_t FrameAllocator::totalCount() { return totalFrames; }
