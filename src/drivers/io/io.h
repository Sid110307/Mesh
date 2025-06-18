#pragma once

#include "../../core/utils.h"

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);

class Serial
{
public:
	static void init();
	static void printf(const char* fmt, ...);

	static void write(const char* str);
	static void write(const char* str, size_t len);
	static void write(uint8_t byte);
	static void writeHex(uint64_t value);
	static void writeDec(uint64_t value);

private:
	static void writeByte(uint8_t byte);
	static inline uint16_t port = 0x3F8;
	static inline bool initialized = false;
};
