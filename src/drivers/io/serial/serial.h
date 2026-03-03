#pragma once

#include <kernel/core/utils.h>

namespace Serial
{
    void init();
    void printf(const char* fmt, ...);

    void printChar(uint8_t c);
    void print(const char* str);
    void printHex(uint64_t value);
    void printDec(uint64_t value);
}
