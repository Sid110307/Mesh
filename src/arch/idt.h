#pragma once

#include "../core/utils.h"

struct __attribute__((packed)) IDTEntry
{
	uint16_t offsetLow, selector;
	uint8_t ist, typeAttr;
	uint16_t offsetMid;
	uint32_t offsetHigh, zero;
};

struct __attribute__((packed)) IDTPointer
{
	uint16_t limit;
	uint64_t base;
};

class IDTManager
{
public:
	static void init();
	static void setEntry(uint8_t vector, void (*isr)(), uint8_t flags = 0x8E, uint8_t ist = 0);

private:
	static IDTEntry idt[256];
};
