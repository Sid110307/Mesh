#include "./paging.h"
#include "../arch/common/spinlock.h"
#include "../boot/limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memory_request;
extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;

static Spinlock frameAllocatorLock;
extern uint8_t _kernel_start[], _kernel_end[];

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
                             const uint64_t flags = PageFlags::PRESENT | PageFlags::RW)
{
	if (!(parent[index] & static_cast<uint64_t>(PageFlags::PRESENT)))
	{
		uint64_t* newTable = createPageTable();
		if (!newTable) return nullptr;

		const uint64_t phys = reinterpret_cast<uint64_t>(newTable) - hhdm_request.response->offset;
		parent[index] = phys | flags;
	}
	return reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (parent[index] & ~0xFFFULL));
}

void Paging::init()
{
	pml4 = createPageTable();
	if (!pml4) return;

	uint64_t kernelPhysStart = kernel_addr_request.response->physical_base;
	uint64_t kernelVirtStart = kernel_addr_request.response->virtual_base;
	uint64_t kernelSize = _kernel_end - _kernel_start;

	for (uint64_t offset = 0; offset < kernelSize; offset += FrameAllocator::SMALL_SIZE)
	{
		uint64_t phys = kernelPhysStart + offset;
		uint64_t virt = kernelVirtStart + offset;

		if (!mapSmall(phys, phys, PageFlags::PRESENT | PageFlags::RW)) return;
		if (!mapSmall(virt, phys, PageFlags::PRESENT | PageFlags::RW)) return;
	}

	if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
	{
		auto* fb = framebuffer_request.response->framebuffers[0];
		auto fbBase = reinterpret_cast<uintptr_t>(fb->address);
		uint64_t fbSize = fb->pitch * fb->height;

		for (uint64_t addr = fbBase; addr < fbBase + fbSize; addr += FrameAllocator::SMALL_SIZE)
			if (!mapSmall(addr, addr, PageFlags::PRESENT | PageFlags::RW)) return;
	}

	uint64_t maxPhys = 0;
	for (size_t i = 0; i < memory_request.response->entry_count; ++i)
		if (auto& entry = memory_request.response->entries[i]; entry->type == LIMINE_MEMMAP_USABLE || entry->type ==
			LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE || entry->type ==
			LIMINE_MEMMAP_FRAMEBUFFER)
			if (uint64_t end = entry->base + entry->length; end > maxPhys) maxPhys = end;

	for (uint64_t phys = 0; phys < maxPhys; phys += FrameAllocator::SMALL_SIZE)
		if (!mapSmall(hhdm_request.response->offset + phys, phys, PageFlags::PRESENT | PageFlags::RW)) return;

	uint64_t pml4Phys = reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset;
	asm volatile ("mov %0, %%cr3" :: "r"(pml4Phys) : "memory");
	uint64_t cr4;
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= 1 << 5;
	asm volatile ("mov %0, %%cr4" :: "r"(cr4));

	uint32_t eax, edx;
	asm volatile (
		"mov $0xC0000080, %%ecx\n"
		"rdmsr\n"
		"or $(1 << 8), %%eax\n"
		"wrmsr\n"
		: "=a"(eax), "=d"(edx)
		:: "ecx");

	uint64_t cr0;
	asm volatile ("mov %%cr0, %0" : "=r"(cr0));
	cr0 |= 1 << 31;
	asm volatile ("mov %0, %%cr0" :: "r"(cr0));
}

bool Paging::mapSmall(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags)
{
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

	pt[ptIndex] = (physicalAddress & ~0xFFFULL) | flags;
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

	return true;
}

bool Paging::mapMedium(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags)
{
	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
	auto pdIndex = (virtualAddress >> 21) & 0x1FF;

	uint64_t* pdpt = ensureTable(pml4, pml4Index);
	if (!pdpt) return false;
	uint64_t* pd = ensureTable(pdpt, pdptIndex);
	if (!pd) return false;

	pd[pdIndex] = (physicalAddress & ~0x1FFFFFULL) | flags | static_cast<uint64_t>(PageFlags::HUGE);
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

	return true;
}

bool Paging::mapLarge(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags)
{
	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;

	uint64_t* pdpt = ensureTable(pml4, pml4Index);
	if (!pdpt) return false;

	pdpt[pdptIndex] = (physicalAddress & ~0x3FFFFFFFULL) | flags | static_cast<uint64_t>(PageFlags::HUGE);
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");

	return true;
}

void Paging::unmapSmall(uint64_t virtualAddress)
{
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
	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;
	auto pdIndex = (virtualAddress >> 21) & 0x1FF;

	if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
	if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pd = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pdpt[pdptIndex] & ~0xFFFULL));
	if (!(pd[pdIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

	pd[pdIndex] = 0;
	asm volatile ("invlpg (%0)" :: "r"(virtualAddress) : "memory");
	cleanup(pd, pdIndex, 2, pdptIndex, pdIndex, pd, pdpt);
}

void Paging::unmapLarge(uint64_t virtualAddress)
{
	auto pml4Index = (virtualAddress >> 39) & 0x1FF;
	auto pdptIndex = (virtualAddress >> 30) & 0x1FF;

	if (!(pml4[pml4Index] & static_cast<uint64_t>(PageFlags::PRESENT))) return;
	auto pdpt = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + (pml4[pml4Index] & ~0xFFFULL));
	if (!(pdpt[pdptIndex] & static_cast<uint64_t>(PageFlags::PRESENT))) return;

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

void FrameAllocator::init(const uint64_t base, const uint64_t size)
{
	memoryBase = base;
	memorySize = size;
	totalFrames = size / SMALL_SIZE;
	usedFrames = 0;
	bitmap = reinterpret_cast<uint64_t*>(hhdm_request.response->offset + base);
	memset(bitmap, 0, (totalFrames + 63) / 64 * sizeof(uint64_t));

	const uint64_t bitmapFrames = ((totalFrames + 63) / 64 * sizeof(uint64_t) + SMALL_SIZE - 1) / SMALL_SIZE;
	for (uint64_t i = 0; i < bitmapFrames; ++i) reserve(reinterpret_cast<void*>(memoryBase + i * SMALL_SIZE));
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
	if (phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	bitmap[index / 64] &= ~(1ULL << (index % 64));
	--usedFrames;
}

void FrameAllocator::reserve(void* frame)
{
	LockGuard guard(frameAllocatorLock);

	const auto addr = reinterpret_cast<uint64_t>(frame);
	const uint64_t phys = addr - hhdm_request.response->offset;
	if (phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	bitmap[index / 64] |= 1ULL << (index % 64);
	++usedFrames;
}

bool FrameAllocator::used(void* frame)
{
	LockGuard guard(frameAllocatorLock);

	const auto addr = reinterpret_cast<uint64_t>(frame);
	const uint64_t phys = addr - hhdm_request.response->offset;
	if (phys >= memoryBase + memorySize || (phys - memoryBase) % SMALL_SIZE != 0) return false;

	const uint64_t index = (phys - memoryBase) / SMALL_SIZE;
	return bitmap[index / 64] & (1ULL << (index % 64));
}

uint64_t FrameAllocator::usedCount() { return usedFrames; }
uint64_t FrameAllocator::totalCount() { return totalFrames; }
