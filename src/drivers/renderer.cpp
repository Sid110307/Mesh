#include "./renderer.h"
#include "../core/limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_hhdm_request hhdm_request;
extern uint8_t _binary__mnt_c_Users_srsfo_Downloads_Mesh_src_assets_fonts_zap_ext_light18_psf_start[];

void Renderer::init()
{
	if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) return;

	const auto* fb = framebuffer_request.response->framebuffers[0];
	fbAddress = reinterpret_cast<uint32_t*>(hhdm_request.response->offset + reinterpret_cast<uint64_t>(fb->address));
	fbWidth = fb->width;
	fbHeight = fb->height;
	fbPitch = fb->pitch;

	auto* header = reinterpret_cast<const PSF1Header*>(_binary__mnt_c_Users_srsfo_Downloads_Mesh_src_assets_fonts_zap_ext_light18_psf_start);
	if (header->magic[0] != 0x36 || header->magic[1] != 0x04)
	{
		fbAddress = nullptr;
		return;
	}

	font.glyphBuffer = _binary__mnt_c_Users_srsfo_Downloads_Mesh_src_assets_fonts_zap_ext_light18_psf_start + sizeof(PSF1Header);
	font.width = 8;
	font.height = header->charSize;
	font.glyphCount = (header->mode & 1) ? 512 : 256;
}

void Renderer::setCursor(const uint32_t x, const uint32_t y)
{
	cursorX = (x >= fbWidth / font.width) ? (fbWidth / font.width - 1) : x;
	cursorY = (y >= fbHeight / font.height) ? (fbHeight / font.height - 1) : y;
}

void Renderer::clear(const uint32_t color)
{
	if (!fbAddress) return;
	for (uint64_t y = 0; y < fbHeight; ++y)
		for (uint64_t x = 0; x < fbWidth; ++x) fbAddress[y * (fbPitch / 4) + x] = color;
	cursorX = cursorY = 0;
}

void Renderer::printChar(const char c, const uint32_t fg, const uint32_t bg)
{
	if (!fbAddress) return;

	if (c == '\n')
	{
		cursorX = 0;
		++cursorY;
		return;
	}
	if (c == '\r')
	{
		cursorX = 0;
		return;
	}

	if (cursorX >= fbWidth / font.width)
	{
		cursorX = 0;
		++cursorY;
	}
	if (cursorY >= fbHeight / font.height)
	{
		// TODO: Implement scrolling
		cursorY = fbHeight / font.height - 1;
	}

	drawGlyph(cursorX * font.width, cursorY * font.height, c, fg, bg);
	++cursorX;
}

void Renderer::printCharAt(const uint32_t x, const uint32_t y, const char c, const uint32_t fg, const uint32_t bg)
{
	if (!fbAddress || c == '\n' || c == '\r') return;
	if (x >= fbWidth / font.width || y >= fbHeight / font.height) return;

	drawGlyph(x * font.width, y * font.height, c, fg, bg);
}

void Renderer::print(const char* str, const uint32_t fgDefault, const uint32_t bgDefault)
{
	uint32_t fg = fgDefault, bg = bgDefault;
	bool inEscape = false;
	char escBuf[ESCAPE_SIZE] = {};
	int escLen = 0;

	for (size_t i = 0; str[i]; ++i)
	{
		const char c = str[i];
		if (inEscape)
		{
			if (escLen < static_cast<int>(sizeof(escBuf) - 1)) escBuf[escLen++] = c;
			if ((c >= '@' && c <= '~') || escLen >= 15)
			{
				escBuf[escLen] = '\0';
				escapeAnsi(escBuf, fg, bg, fgDefault, bgDefault);
				inEscape = false;
				escLen = 0;
			}
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

inline void Renderer::drawGlyph(const uint32_t px, const uint32_t py, const char c, const uint32_t fg,
                                const uint32_t bg)
{
	if (static_cast<uint8_t>(c) >= font.glyphCount) return;

	const uint8_t* glyph = font.glyphBuffer + static_cast<uint8_t>(c) * font.height;
	for (uint32_t y = 0; y < font.height && py + y < fbHeight; ++y)
		for (uint32_t x = 0; x < font.width && px + x < fbWidth; ++x)
			fbAddress[(py + y) * (fbPitch / 4) + (px + x)] = (glyph[y] & (1 << (7 - x))) ? fg : bg;
}

void Renderer::escapeAnsi(const char* seq, uint32_t& fg, uint32_t& bg, const uint32_t fgDefault,
                          const uint32_t bgDefault)
{
	if (seq[0] != '[') return;

	char buf[ESCAPE_SIZE];
	size_t len = 0;
	for (size_t i = 1; seq[i] && len < sizeof(buf) - 1; ++i) buf[len++] = seq[i];
	buf[len] = '\0';

	const char* token = strtok(buf, ";");
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
				break;
		}
		token = strtok(nullptr, ";");
	}
}
