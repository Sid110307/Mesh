#pragma once

#include "../core/utils.h"

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

constexpr uint64_t operator|(PageFlags a, PageFlags b) { return static_cast<uint64_t>(a) | static_cast<uint64_t>(b); }
constexpr uint64_t operator&(PageFlags a, PageFlags b) { return static_cast<uint64_t>(a) & static_cast<uint64_t>(b); }
constexpr uint64_t operator~(PageFlags flag) { return ~static_cast<uint64_t>(flag); }

class Paging
{
public:
	static void init();
	static void map(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags);
	static void unmap(uint64_t virtualAddress);
};

class FrameAllocator
{
public:
	static void init(uint64_t base, uint64_t size);
	static void* alloc();
	static void free(void* frame);
	static void reserve(void* frame);
	static bool used(void* frame);

	static uint64_t usedCount();
	static uint64_t totalCount();
	static constexpr uint64_t FRAME_SIZE = 4096;
};
