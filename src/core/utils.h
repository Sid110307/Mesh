#pragma once

#ifndef __UINT8_TYPE__
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long size_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef long ssize_t;
#else
#include <cstdint>
#include <cstddef>
#endif

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

int atoi(const char* str);
const char* itoa(uint64_t value);
char* strchr(const char* str, int c);
char* strtok(char* str, const char* delim);
