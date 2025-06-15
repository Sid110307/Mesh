#include "./idt.h"
#include "./isr.h"

IDTEntry IDTManager::idt[256];
extern "C" void* const isrList[32] = {
	(void*)isr0, (void*)isr1, (void*)isr2, (void*)isr3, (void*)isr4, (void*)isr5, (void*)isr6, (void*)isr7, (void*)isr8,
	(void*)isr9, (void*)isr10, (void*)isr11, (void*)isr12, (void*)isr13, (void*)isr14, (void*)isr15, (void*)isr16,
	(void*)isr17, (void*)isr18, (void*)isr19, (void*)isr20, (void*)isr21, (void*)isr22, (void*)isr23, (void*)isr24,
	(void*)isr25, (void*)isr26, (void*)isr27, (void*)isr28, (void*)isr29, (void*)isr30, (void*)isr31,
};

void IDTManager::init()
{
	for (int i = 0; i < 32; ++i) setEntry(i, reinterpret_cast<void (*)()>(isrList[i]));

	IDTPointer idtPtr = {
		.limit = sizeof(idt) - 1,
		.base = reinterpret_cast<uint64_t>(idt),
	};
	asm volatile("lidt %0" :: "m"(idtPtr));
	asm volatile("sti");
}

void IDTManager::setEntry(const uint8_t vector, void (*isr)(), const uint8_t flags, const uint8_t ist)
{
	if (!isr)
	{
		idt[vector] = {};
		return;
	}

	const auto addr = reinterpret_cast<uint64_t>(isr);
	idt[vector] = {
		.offsetLow = static_cast<uint16_t>(addr & 0xFFFF),
		.selector = 0x08,
		.ist = ist,
		.typeAttr = flags,
		.offsetMid = static_cast<uint16_t>((addr >> 16) & 0xFFFF),
		.offsetHigh = static_cast<uint32_t>((addr >> 32) & 0xFFFFFFFF),
		.zero = 0
	};
}
