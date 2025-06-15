#pragma once

#include "../core/utils.h"

enum : uint16_t
{
	GDT_NULL    = 0,
	GDT_CODE    = 1,
	GDT_DATA    = 2,
	GDT_TSS     = 3,
	GDT_ENTRIES = 5
};

struct __attribute__((packed)) GDTEntry
{
	uint16_t limitLow, baseLow;
	uint8_t baseMid, access, flagsLimitHigh, baseHigh;
};

struct __attribute__((packed)) GDTPointer
{
	uint16_t limit;
	uint64_t base;
};

struct __attribute__((packed)) TSS
{
	uint32_t reserved0;
	uint64_t rsp[3], reserved1, ist[7], reserved2;
	uint16_t reserved3, ioMapBase;
};

class GDTManager
{
public:
	static GDTManager& getInstance();
	static void init();
	static void load();
	static void setTSS(uint64_t rsp0);

private:
	static GDTEntry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
	static GDTPointer gdtPointer;
	static TSS kernelTSS __attribute__((aligned(16)));

	GDTManager() = default;
	GDTManager(const GDTManager&) = delete;
	GDTManager& operator=(const GDTManager&) = delete;

	static void setEntry(uint16_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
};
