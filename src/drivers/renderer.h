#pragma once

#include "../core/utils.h"

struct PSF1Header
{
	uint8_t magic[2], mode, charSize;
} __attribute__((packed));

struct Font
{
	const uint8_t* glyphBuffer;
	uint32_t width, height, glyphCount;
};

class Renderer
{
public:
	static void init();
	static void setCursor(uint32_t x, uint32_t y);
	static void clear(uint32_t color);
	static void printChar(char c, uint32_t fg = WHITE, uint32_t bg = BLACK);
	static void printCharAt(uint32_t x, uint32_t y, char c, uint32_t fg = WHITE, uint32_t bg = BLACK);
	static void print(const char* str, uint32_t fg = WHITE, uint32_t bg = BLACK);
	static void printAt(uint32_t x, uint32_t y, const char* str, uint32_t fg = WHITE, uint32_t bg = BLACK);
	static void scroll();

private:
	inline static void drawGlyph(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg);
	static void escapeAnsi(const char* seq, uint32_t& fg, uint32_t& bg, uint32_t fgDefault, uint32_t bgDefault);

	inline static uint64_t fbWidth = 0, fbHeight = 0, fbPitch = 0;
	inline static uint32_t* fbAddress = nullptr;
	inline static uint32_t cursorX = 0, cursorY = 0;
	inline static Font font;
};
