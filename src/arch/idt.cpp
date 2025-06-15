#include "./idt.h"

extern "C" void isr0();
extern "C" void isr8();
extern "C" void isr14();

IDTEntry IDTManager::idt[256];
IDTPointer IDTManager::idtPtr;

void IDTManager::init()
{
	memset(idt, 0, sizeof(idt));

	setEntry(0, isr0);
	setEntry(8, isr8, 1);
	setEntry(14, isr14);

	idtPtr.limit = sizeof(idt) - 1;
	idtPtr.base = reinterpret_cast<uint64_t>(&idt);
	load();
}

void IDTManager::load() { asm volatile ("lidt %0" :: "m"(idtPtr)); }

void IDTManager::setEntry(const uint8_t vector, void (*isr)(), const uint8_t ist)
{
	if (!isr)
	{
		idt[vector] = {};
		return;
	}

	const auto addr = reinterpret_cast<uint64_t>(isr);
	idt[vector].offsetLow = addr & 0xFFFF;
	idt[vector].selector = 0x08;
	idt[vector].ist = ist & 0x7;
	idt[vector].typeAttr = 0x8E;
	idt[vector].offsetMid = (addr >> 16) & 0xFFFF;
	idt[vector].offsetHigh = (addr >> 32) & 0xFFFFFFFF;
	idt[vector].zero = 0;
}
