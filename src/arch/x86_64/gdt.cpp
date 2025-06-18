#include "./gdt.h"

#include <memory/paging.h>

#include "../../core/utils.h"
#include "../../boot/limine.h"

extern limine_hhdm_request hhdm_request;

extern "C"
{
GDTEntry gdt[GDT_ENTRIES] __attribute__((aligned(8))) = {};
GDTPointer gdtPointer = {};
}

static uint8_t istStacks[7][8192] __attribute__((aligned(16)));

GDTManager::GDTManager()
{
	setEntry(GDT_NULL, 0, 0, 0, 0);
	setEntry(GDT_CODE, 0, 0, 0x9A, 0x20);
	setEntry(GDT_DATA, 0, 0, 0x92, 0x00);
	setEntry(GDT_TSS, 0, 0, 0x89, 0x00);
	setEntry(GDT_TSS + 1, 0, 0, 0x00, 0x00);

	gdtPointer.limit = sizeof(gdt) - 1;
	gdtPointer.base = reinterpret_cast<uint64_t>(gdt);
}

void GDTManager::load()
{
	uint16_t dataSel = GDT_DATA << 3, codeSel = GDT_CODE << 3;

	asm volatile ("lgdt %0" :: "m"(gdtPointer) : "memory");
	asm volatile (
		"mov %[sel], %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%es\n"
		"mov %%ax, %%fs\n"
		"mov %%ax, %%gs\n"
		"mov %%ax, %%ss\n"
		:: [sel] "r"(dataSel)
		: "rax"
	);
	asm volatile (
		"mov %[cs], %%ax\n"
		"pushq %%rax\n"
		"lea 1f(%%rip), %%rax\n"
		"pushq %%rax\n"
		"retfq\n"
		"1:\n"
		:: [cs] "r"(codeSel)
		: "rax", "memory"
	);
}

void GDTManager::setTSS(uint64_t rsp0)
{
	memset(&kernelTSS, 0, sizeof(kernelTSS));
	kernelTSS.rsp[0] = rsp0;
	kernelTSS.ioMapBase = sizeof(kernelTSS);
	for (int i = 0; i < 7; ++i) kernelTSS.ist[i] = reinterpret_cast<uint64_t>(&istStacks[i][8192]);

	auto base = reinterpret_cast<uint64_t>(&kernelTSS);
	uint32_t limit = sizeof(TSS) - 1;

	gdt[GDT_TSS].limitLow = limit & 0xFFFF;
	gdt[GDT_TSS].baseLow = base & 0xFFFF;
	gdt[GDT_TSS].baseMid = (base >> 16) & 0xFF;
	gdt[GDT_TSS].access = 0x89;
	gdt[GDT_TSS].flagsLimitHigh = (limit >> 16) & 0x0F;
	gdt[GDT_TSS].baseHigh = (base >> 24) & 0xFF;

	*reinterpret_cast<uint32_t*>(&gdt[GDT_TSS + 1]) = (base >> 32) & 0xFFFFFFFF;
	*(reinterpret_cast<uint32_t*>(&gdt[GDT_TSS + 1]) + 1) = 0;

	asm volatile ("ltr %0" :: "r"(static_cast<uint16_t>(GDT_TSS << 3)));
}

void GDTManager::setEntry(const uint16_t index, const uint32_t base, const uint32_t limit, const uint8_t access,
                          const uint8_t flags)
{
	gdt[index].limitLow = limit & 0xFFFF;
	gdt[index].baseLow = base & 0xFFFF;
	gdt[index].baseMid = (base >> 16) & 0xFF;
	gdt[index].access = access;
	gdt[index].flagsLimitHigh = ((limit >> 16) & 0x0F) | (flags & 0xF0);
	gdt[index].baseHigh = (base >> 24) & 0xFF;
}
