#pragma once

#include <cstdint>
#include <cstddef>
#include <climits>
#include <cstdarg>

enum Color
{
	BLACK         = 0x000000,
	RED           = 0xFF0000,
	GREEN         = 0x00FF00,
	YELLOW        = 0xFFFF00,
	BLUE          = 0x0000FF,
	MAGENTA       = 0xFF00FF,
	CYAN          = 0x00FFFF,
	WHITE         = 0xFFFFFF,
	LIGHT_BLACK   = 0x808080,
	LIGHT_RED     = 0xFF8080,
	LIGHT_GREEN   = 0x80FF80,
	LIGHT_YELLOW  = 0xFFFF80,
	LIGHT_BLUE    = 0x8080FF,
	LIGHT_MAGENTA = 0xFF80FF,
	LIGHT_CYAN    = 0x80FFFF,
	LIGHT_WHITE   = 0xE0E0E0,
	DEFAULT       = 0xE0E0E0,
};

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);

int atoi(const char* str);
char* utoa(uint64_t value, char* buffer, size_t bufferSize, uint8_t base = 10, bool uppercase = true);
char* strchr(const char* str, int c);
char* strtok_r(char* str, const char* delim, char** savePtr);

void swap(uint32_t& a, uint32_t& b) noexcept;
uint64_t ffsll(uint64_t value) noexcept;

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);

using putCharFn = void(*)(char);
using putStrFn = void(*)(const char*);
using putHexFn = void(*)(uint64_t);
using putDecFn = void(*)(uint64_t);

void vformat(const char* fmt, va_list args, putCharFn putc, putStrFn puts, putHexFn putHex, putDecFn putDec);
