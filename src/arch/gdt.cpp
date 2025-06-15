#include "./gdt.h"

#include "../core/utils.h"

GDTEntry GDTManager::gdt[GDT_ENTRIES] __attribute__((aligned(8)));
GDTPointer GDTManager::gdtPointer;
TSS GDTManager::kernelTSS __attribute__((aligned(16)));

static uint8_t ist1Stack[4096] __attribute__((aligned(16)));
static uint64_t ist1StackTop = reinterpret_cast<uint64_t>(ist1Stack + sizeof(ist1Stack));

GDTManager& GDTManager::getInstance()
{
	static GDTManager instance;
	return instance;
}

void GDTManager::init()
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
		"mov %[cs], %%ax\n\t"
		"pushq %%rax\n\t"
		"lea 1f(%%rip), %%rax\n\t"
		"pushq %%rax\n\t"
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

	extern uint64_t ist1StackTop;
	kernelTSS.ist[0] = ist1StackTop;

	auto base = reinterpret_cast<uint64_t>(&kernelTSS);
	uint32_t limit = sizeof(kernelTSS) - 1;
	setEntry(GDT_TSS, static_cast<uint32_t>(base), limit, 0x89, 0x00);

	gdt[GDT_TSS + 1].limitLow = limit >> 16;
	gdt[GDT_TSS + 1].baseLow = base >> 48;
	gdt[GDT_TSS + 1].baseMid = (base >> 32) & 0xFF;
	gdt[GDT_TSS + 1].access = 0;
	gdt[GDT_TSS + 1].flagsLimitHigh = 0;
	gdt[GDT_TSS + 1].baseHigh = base >> 56;

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
