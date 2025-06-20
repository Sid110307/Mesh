#pragma once

#include <arch/common/spinlock.h>
#include <core/utils.h>

class Serial
{
public:
	static void init();
	static void printf(const char* fmt, ...);

	static void printChar(uint8_t c);
	static void print(const char* str);
	static void printHex(uint64_t value);
	static void printDec(uint64_t value);

private:
	static void printCharUnlocked(uint8_t c);
	static void printUnlocked(const char* str);
	static void printHexUnlocked(uint64_t value);
	static void printDecUnlocked(uint64_t value);

	inline static uint16_t port = 0x3F8;
	inline static bool initialized = false;
	static Spinlock serialLock;
};
