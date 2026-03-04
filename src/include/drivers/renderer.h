#pragma once

#include <core/utils.h>

struct Font
{
    const uint8_t* glyphBuffer;
    uint32_t width, height, glyphCount;
};

namespace Renderer
{
    void init();
    void escapeAnsi(const char* seq, uint32_t& fg, uint32_t& bg, uint32_t fgDefault, uint32_t bgDefault);
    void clear(uint32_t color);
    void printf(const char* fmt, ...);

    void printChar(char c, uint32_t fg, uint32_t bg);
    void printCharAt(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void print(const char* str, uint32_t fg, uint32_t bg);
    void printAt(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
    void printHex(uint64_t value, uint32_t fg, uint32_t bg);
    void printHexAt(uint32_t x, uint32_t y, uint64_t value, uint32_t fg, uint32_t bg);
    void printDec(uint64_t value, uint32_t fg, uint32_t bg);
    void printDecAt(uint32_t x, uint32_t y, uint64_t value, uint32_t fg, uint32_t bg);
    void scroll();

    void setCursor(uint32_t x, uint32_t y);
    uint32_t getCursorX();
    uint32_t getCursorY();

    uint32_t getFontWidth();
    uint32_t getFontHeight();

    bool getSerialPrint();
    void setSerialPrint(bool value);
}
