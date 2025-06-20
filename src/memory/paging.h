#pragma once

#include <arch/common/spinlock.h>
#include <core/utils.h>

enum class PageFlags : uint64_t
{
	NONE          = 0,
	PRESENT       = 1ULL << 0,
	RW            = 1ULL << 1,
	USER          = 1ULL << 2,
	WRITE_THROUGH = 1ULL << 3,
	CACHE_DISABLE = 1ULL << 4,
	ACCESSED      = 1ULL << 5,
	DIRTY         = 1ULL << 6,
	HUGE          = 1ULL << 7,
	GLOBAL        = 1ULL << 8,
	NO_EXECUTE    = 1ULL << 63
};

constexpr PageFlags operator|(PageFlags a, PageFlags b)
{
	return static_cast<PageFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

constexpr PageFlags operator&(PageFlags a, PageFlags b)
{
	return static_cast<PageFlags>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

constexpr PageFlags operator~(PageFlags flag) { return static_cast<PageFlags>(~static_cast<uint64_t>(flag)); }

extern uint64_t* pml4;

class Paging
{
public:
	static void init();

	static bool mapSmall(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags);
	static bool mapMedium(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags);
	static bool mapLarge(uint64_t virtualAddress, uint64_t physicalAddress, PageFlags flags);

	static void unmapSmall(uint64_t virtualAddress);
	static void unmapMedium(uint64_t virtualAddress);
	static void unmapLarge(uint64_t virtualAddress);

private:
	static void cleanup(uint64_t* startTable, uint16_t startIndex, int startLevel, uint64_t pdptIndex, uint64_t pdIndex,
	                    const uint64_t* pd, const uint64_t* pdpt);
	static bool cleanupPageTable(uint64_t* rootTable, uint16_t rootIndex, int rootLevel);

	static Spinlock pagingLock;
};

class FrameAllocator
{
public:
	static void init();
	static void* alloc();
	static void free(void* frame);
	static void reserve(void* frame);
	static bool used(void* frame);

	static uint64_t usedCount();
	static uint64_t totalCount();
	static Spinlock frameAllocatorLock;

	static constexpr uint64_t SMALL_SIZE = 4096, MEDIUM_SIZE = 2ULL * 1024 * 1024,
	                          LARGE_SIZE = 1ULL * 1024 * 1024 * 1024, BIOS_START = 0x100000;
};
