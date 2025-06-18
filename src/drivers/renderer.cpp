#include "./renderer.h"
#include "./io.h"
#include "../core/limine.h"

extern limine_framebuffer_request framebuffer_request;
extern uint8_t asset_src_assets_fonts_zap_ext_light18_psf_start[];

void Renderer::init()
{
	if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
	{
		Serial::printf("Renderer: No framebuffer available.\n");
		return;
	}

	const auto* fb = framebuffer_request.response->framebuffers[0];
	if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || fb->bpp != 32)
	{
		Serial::printf("Renderer: Unsupported framebuffer format (memory model: %u, bpp: %u).\n", fb->memory_model,
		               fb->bpp);
		fbAddress = nullptr;

		return;
	}

	fbAddress = static_cast<uint32_t*>(fb->address);
	fbWidth = fb->width;
	fbHeight = fb->height;
	fbPitch = fb->pitch;

	auto* header = reinterpret_cast<const PSF1Header*>(asset_src_assets_fonts_zap_ext_light18_psf_start);
	if (header->magic[0] != 0x36 || header->magic[1] != 0x04)
	{
		Serial::printf("Renderer: Invalid PSF1 font header.\n");
		fbAddress = nullptr;

		return;
	}

	font.glyphBuffer = asset_src_assets_fonts_zap_ext_light18_psf_start + sizeof(PSF1Header);
	font.width = 8;
	font.height = header->charSize;
	font.glyphCount = (header->mode & 1) ? 512 : 256;
}

void Renderer::scroll()
{
	if (!fbAddress || font.height == 0 || font.width == 0)
	{
		Serial::printf("Renderer: Cannot scroll, framebuffer or font not initialized.\n");
		return;
	}

	const size_t bytesPerLine = fbPitch;
	const size_t scrollBytes = (fbHeight - font.height) * bytesPerLine;

	memmove(fbAddress, reinterpret_cast<uint8_t*>(fbAddress) + bytesPerLine, scrollBytes);
	memset(reinterpret_cast<uint8_t*>(fbAddress) + scrollBytes, 0, font.height * bytesPerLine);
}

void Renderer::clear(const uint32_t color)
{
	if (!fbAddress)
	{
		Serial::printf("Renderer: Cannot clear, framebuffer not initialized.\n");
		return;
	}

	const size_t totalPixels = (fbPitch / 4) * fbHeight;
	for (size_t i = 0; i < totalPixels; ++i) fbAddress[i] = color;
	cursorX = cursorY = 0;
}

void Renderer::printf(const char* fmt, ...)
{
	if (!fbAddress)
	{
		Serial::printf("Renderer: Cannot print, framebuffer not initialized.\n");
		return;
	}
	if (!fmt || !*fmt)
	{
		Serial::printf("Renderer: Invalid format string.\n");
		return;
	}

	va_list args;
	va_start(args, fmt);
	vformat(fmt, args, [](const char c) { ansiPutChar(c); }, [](const char* s) { print(s); },
	        [](const uint64_t h) { printHex(h); }, [](const uint64_t d) { printDec(d); });
	va_end(args);
}

void Renderer::printChar(const char c, const uint32_t fg, const uint32_t bg)
{
	if (!fbAddress)
	{
		Serial::printf("Renderer: Cannot print character, framebuffer not initialized.\n");
		return;
	}

	if (c == '\n')
	{
		cursorX = 0;
		++cursorY;

		if (serialPrint) Serial::printf("\n");
		return;
	}
	if (c == '\r')
	{
		cursorX = 0;
		if (serialPrint) Serial::printf("\r");

		return;
	}
	if (c == '\t')
	{
		cursorX = (cursorX + tabWidth) & ~(tabWidth - 1);
		if (cursorX >= fbWidth / font.width)
		{
			cursorX = 0;
			++cursorY;
		}
		if (cursorY >= fbHeight / font.height)
		{
			scroll();
			cursorY--;
		}

		if (serialPrint) Serial::printf("\t");
		return;
	}

	if (cursorX >= fbWidth / font.width)
	{
		cursorX = 0;
		++cursorY;
	}
	if (cursorY >= fbHeight / font.height)
	{
		scroll();
		cursorY--;
	}

	drawGlyph(cursorX * font.width, cursorY * font.height, c, fg, bg);
	++cursorX;

	if (serialPrint) Serial::printf("%c", c);
}

void Renderer::printCharAt(const uint32_t x, const uint32_t y, const char c, const uint32_t fg, const uint32_t bg)
{
	if (!fbAddress || c == '\n' || c == '\r' || c == '\t')
	{
		Serial::printf("Renderer: Cannot print character, framebuffer not initialized or invalid character.\n");
		return;
	}
	if (x >= fbWidth / font.width || y >= fbHeight / font.height)
	{
		Serial::printf("Renderer: Invalid position for character.\n");
		return;
	}

	drawGlyph(x * font.width, y * font.height, c, fg, bg);
	if (serialPrint) Serial::printf("%c", c);
}

void Renderer::print(const char* str, const uint32_t fgDefault, const uint32_t bgDefault)
{
	uint32_t fg = fgDefault, bg = bgDefault;
	bool inEscape = false;
	char escBuf[16];
	int escLen = 0;

	for (const char* p = str; *p; ++p)
	{
		const char c = *p;

		if (inEscape)
		{
			if (c == 'm')
			{
				escBuf[escLen] = '\0';
				if (escLen > 0 && escBuf[0] == '[') escapeAnsi(escBuf + 1, fg, bg, fgDefault, bgDefault);
				inEscape = false;
				escLen = 0;

				continue;
			}
			if (escLen + 1 < static_cast<int>(sizeof(escBuf))) escBuf[escLen++] = c;

			continue;
		}
		if (c == '\x1B')
		{
			inEscape = true;
			escLen = 0;

			continue;
		}

		printChar(c, fg, bg);
	}
}

void Renderer::printAt(const uint32_t x, const uint32_t y, const char* str, const uint32_t fgDefault,
                       const uint32_t bgDefault)
{
	setCursor(x, y);
	print(str, fgDefault, bgDefault);
}

void Renderer::printHex(const uint64_t value, const uint32_t fg, const uint32_t bg)
{
	char buffer[33];
	print(utoa(value, buffer, sizeof(buffer), 16), fg, bg);
}

void Renderer::printHexAt(const uint32_t x, const uint32_t y, const uint64_t value, const uint32_t fg,
                          const uint32_t bg)
{
	setCursor(x, y);
	printHex(value, fg, bg);
}

void Renderer::printDec(const uint64_t value, const uint32_t fg, const uint32_t bg)
{
	char buffer[33];
	print(utoa(value, buffer, sizeof(buffer)), fg, bg);
}

void Renderer::printDecAt(const uint32_t x, const uint32_t y, const uint64_t value, const uint32_t fg,
                          const uint32_t bg)
{
	setCursor(x, y);
	printDec(value, fg, bg);
}

void Renderer::setCursor(const uint32_t x, const uint32_t y)
{
	cursorX = x >= fbWidth / font.width ? fbWidth / font.width - 1 : x;
	cursorY = y >= fbHeight / font.height ? fbHeight / font.height - 1 : y;
}

uint32_t Renderer::getCursorX() { return cursorX; }
uint32_t Renderer::getCursorY() { return cursorY; }
uint32_t Renderer::getFontWidth() { return font.width; }
uint32_t Renderer::getFontHeight() { return font.height; }

bool Renderer::getSerialPrint() { return serialPrint; }
void Renderer::setSerialPrint(const bool value) { serialPrint = value; }

void Renderer::ansiPutChar(const char c)
{
	static bool inEscape = false;
	static char escBuf[16];
	static size_t escLen = 0;

	if (inEscape)
	{
		if (escLen < sizeof(escBuf) - 1)
			escBuf[escLen++] = c;

		if (c == 'm')
		{
			escBuf[escLen] = '\0';
			if (escBuf[0] == '[') escapeAnsi(escBuf + 1, ansiFg, ansiBg, WHITE, BLACK);

			inEscape = false;
			escLen = 0;
		}
		else if (escLen >= sizeof(escBuf) - 1)
		{
			inEscape = false;
			escLen = 0;
		}

		return;
	}

	if (c == '\x1B')
	{
		inEscape = true;
		escLen = 0;
		return;
	}

	printChar(c, ansiFg, ansiBg);
}

inline void Renderer::drawGlyph(const uint32_t px, const uint32_t py, const char c, const uint32_t fg,
                                const uint32_t bg)
{
	if (!fbAddress || !font.glyphBuffer)
	{
		Serial::printf("Renderer: Cannot draw glyph, framebuffer or font not initialized.\n");
		return;
	}
	if (font.height == 0 || font.width == 0 || static_cast<uint8_t>(c) >= font.glyphCount || px +
		font.width > fbWidth || py + font.height > fbHeight)
	{
		Serial::printf("Renderer: Invalid glyph or position for drawing.\n");
		return;
	}

	const uint8_t* glyph = font.glyphBuffer + static_cast<uint8_t>(c) * font.height;
	for (uint32_t y = 0; y < font.height && py + y < fbHeight; ++y)
		for (uint32_t x = 0; x < font.width && px + x < fbWidth; ++x)
			fbAddress[(py + y) * (fbPitch / 4) + (px + x)] = (glyph[y] & (1 << (7 - x))) ? fg : bg;
}

void Renderer::escapeAnsi(const char* seq, uint32_t& fg, uint32_t& bg, const uint32_t fgDefault,
                          const uint32_t bgDefault)
{
	char buf[16];
	size_t len = 0;
	for (size_t i = 0; seq[i] && len < sizeof(buf) - 1; ++i) buf[len++] = seq[i];
	buf[len] = '\0';

	char* savePtr = nullptr;
	const char* token = strtok_r(buf, ";", &savePtr);
	while (token)
	{
		switch (atoi(token))
		{
			case 0:
				fg = fgDefault;
				bg = bgDefault;

				break;
			case 2:
				clear(bg);
				break;
			case 30:
				fg = BLACK;
				break;
			case 31:
				fg = RED;
				break;
			case 32:
				fg = GREEN;
				break;
			case 33:
				fg = YELLOW;
				break;
			case 34:
				fg = BLUE;
				break;
			case 35:
				fg = MAGENTA;
				break;
			case 36:
				fg = CYAN;
				break;
			case 37:
				fg = WHITE;
				break;
			case 40:
				bg = BLACK;
				break;
			case 41:
				bg = RED;
				break;
			case 42:
				bg = GREEN;
				break;
			case 43:
				bg = YELLOW;
				break;
			case 44:
				bg = BLUE;
				break;
			case 45:
				bg = MAGENTA;
				break;
			case 46:
				bg = CYAN;
				break;
			case 47:
				bg = WHITE;
				break;
			case 90:
				fg = LIGHT_BLACK;
				break;
			case 91:
				fg = LIGHT_RED;
				break;
			case 92:
				fg = LIGHT_GREEN;
				break;
			case 93:
				fg = LIGHT_YELLOW;
				break;
			case 94:
				fg = LIGHT_BLUE;
				break;
			case 95:
				fg = LIGHT_MAGENTA;
				break;
			case 96:
				fg = LIGHT_CYAN;
				break;
			case 97:
				fg = LIGHT_WHITE;
				break;
			case 100:
				bg = LIGHT_BLACK;
				break;
			case 101:
				bg = LIGHT_RED;
				break;
			case 102:
				bg = LIGHT_GREEN;
				break;
			case 103:
				bg = LIGHT_YELLOW;
				break;
			case 104:
				bg = LIGHT_BLUE;
				break;
			case 105:
				bg = LIGHT_MAGENTA;
				break;
			case 106:
				bg = LIGHT_CYAN;
				break;
			case 107:
				bg = LIGHT_WHITE;
				break;
			default:
				Serial::printf("Renderer: Unsupported ANSI escape code: %s\n", token);
				break;
		}
		token = strtok_r(nullptr, ";", &savePtr);
	}
}
