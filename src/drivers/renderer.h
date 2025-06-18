#pragma once

#include "../core/utils.h"

struct __attribute__((packed)) PSF1Header
{
	uint8_t magic[2], mode, charSize;
};

struct Font
{
	const uint8_t* glyphBuffer;
	uint32_t width, height, glyphCount;
};

class Renderer
{
public:
	static void init();
	static void escapeAnsi(const char* seq, uint32_t& fg, uint32_t& bg, uint32_t fgDefault, uint32_t bgDefault);
	static void clear(uint32_t color);
	static void printf(const char* fmt, ...);

	static void printChar(char c, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printCharAt(uint32_t x, uint32_t y, char c, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void print(const char* str, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printAt(uint32_t x, uint32_t y, const char* str, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printHex(uint64_t value, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printHexAt(uint32_t x, uint32_t y, uint64_t value, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printDec(uint64_t value, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void printDecAt(uint32_t x, uint32_t y, uint64_t value, uint32_t fg = ansiFg, uint32_t bg = ansiBg);
	static void scroll();

	static void setCursor(uint32_t x, uint32_t y);
	static uint32_t getCursorX();
	static uint32_t getCursorY();

	static uint32_t getFontWidth();
	static uint32_t getFontHeight();

	static bool getSerialPrint();
	static void setSerialPrint(bool value);

private:
	static void ansiPutChar(char c);
	inline static void drawGlyph(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg);

	inline static bool serialPrint = false;
	inline static uint64_t fbWidth = 0, fbHeight = 0, fbPitch = 0;
	inline static uint32_t* fbAddress = nullptr;
	inline static uint32_t ansiFg = WHITE, ansiBg = BLACK, cursorX = 0, cursorY = 0, tabWidth = 4;
	inline static Font font;
};
