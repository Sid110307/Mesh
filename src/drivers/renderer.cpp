#include <core/limine.h>
#include <drivers/renderer.h>
#include <drivers/serial.h>
#include <memory/spinlock.h>

struct __attribute__ ((packed)) PSF1Header
{
    uint8_t magic[2], mode, charSize;
};

struct AnsiState
{
    bool inEscape = false;
    char buffer[32] = {};
    int length = 0;
};

extern limine_framebuffer_request framebuffer_request;
extern uint8_t asset_src_assets_fonts_zap_ext_light18_psf_start[];

bool serialPrint = false;
uint64_t fbWidth = 0, fbHeight = 0, fbPitch = 0;
uint32_t* fbAddress = nullptr;
uint32_t ansiFg = WHITE, ansiBg = BLACK, cursorX = 0, cursorY = 0, tabWidth = 4;
Font font;
Spinlock renderLock;

bool fbReady()
{
    return fbAddress && font.glyphBuffer && font.width > 0 && font.height > 0 && fbWidth > 0 && fbHeight > 0 && fbPitch
        > 0;
}

void drawGlyph(const uint32_t px, const uint32_t py, const char c, const uint32_t fg,
               const uint32_t bg)
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot draw glyph, framebuffer or font not initialized\n");
        return;
    }
    if (font.height == 0 || font.width == 0 || static_cast<uint8_t>(c) >= font.glyphCount ||
        px + font.width > fbWidth || py + font.height > fbHeight)
    {
        Serial::printf("Renderer: Invalid glyph or position for drawing\n");
        return;
    }

    const uint8_t* glyph = font.glyphBuffer + static_cast<uint8_t>(c) * font.height;
    for (uint32_t y = 0; y < font.height && py + y < fbHeight; ++y)
        for (uint32_t x = 0; x < font.width && px + x < fbWidth; ++x)
            fbAddress[(py + y) * (fbPitch / sizeof(uint32_t)) + (px + x)] = glyph[y] & (1 << (7 - x)) ? fg : bg;
}

bool processAnsi(AnsiState& s, char c,
                 uint32_t& fg, uint32_t& bg)
{
    if (!s.inEscape)
    {
        if (c == '\x1b')
        {
            s.inEscape = true;
            s.length = 0;

            return true;
        }
        return false;
    }

    if (s.length == 0 && c != '[')
    {
        s.inEscape = false;
        return false;
    }

    s.buffer[s.length++] = c;
    s.buffer[s.length] = '\0';
    if (c != 'm') return true;
    s.inEscape = false;

    static const uint32_t basic[] = {0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA};
    static const uint32_t bright[] = {0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF};

    const char* p = s.buffer + 1;
    while (*p)
    {
        int code = 0;
        while (*p >= '0' && *p <= '9') code = code * 10 + (*p++ - '0');
        if (*p == ';' || *p == 'm') p++;

        if (code == 0)
        {
            fg = WHITE;
            bg = BLACK;
        }
        else if (code == 7)
        {
            const uint32_t t = fg;

            fg = bg;
            bg = t;
        }
        else if (code >= 30 && code <= 37) fg = basic[code - 30];
        else if (code >= 40 && code <= 47) bg = basic[code - 40];
        else if (code >= 90 && code <= 97) fg = bright[code - 90];
        else if (code >= 100 && code <= 107) bg = bright[code - 100];
    }
    return true;
}

void clearUnlocked(const uint32_t color)
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot clear, framebuffer not initialized\n");
        return;
    }

    for (size_t i = 0; i < fbPitch / sizeof(uint32_t) * fbHeight; ++i) fbAddress[i] = color;
    cursorX = cursorY = 0;
}

void scrollUnlocked()
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot scroll, framebuffer or font not initialized\n");
        return;
    }
    const size_t bytesPerLine = fbPitch, scrollBytes = (fbHeight - font.height) * bytesPerLine;

    memmove(fbAddress, reinterpret_cast<uint8_t*>(fbAddress) + font.height * bytesPerLine, scrollBytes);
    memset(reinterpret_cast<uint8_t*>(fbAddress) + scrollBytes, 0, font.height * bytesPerLine);
}

void printCharUnlocked(const char c, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot print character, framebuffer not initialized\n");
        return;
    }

    if (c == '\n')
    {
        cursorX = 0;
        ++cursorY;
        if (cursorY >= fbHeight / font.height)
        {
            scrollUnlocked();
            cursorY--;
        }

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
            scrollUnlocked();
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
        scrollUnlocked();
        cursorY--;
    }

    drawGlyph(cursorX * font.width, cursorY * font.height, c, fg, bg);
    ++cursorX;

    if (serialPrint) Serial::printf("%c", c);
}

void printUnlocked(const char* str, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot print, framebuffer not initialized\n");
        return;
    }
    if (!str)
    {
        Serial::printf("Renderer: Invalid string\n");
        return;
    }

    AnsiState state;
    uint32_t fgColor = fg, bgColor = bg;

    for (size_t i = 0; str[i]; ++i)
        if (!processAnsi(state, str[i], fgColor, bgColor)) printCharUnlocked(str[i], fgColor, bgColor);
}

void printHexUnlocked(const uint64_t value, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    char buffer[33];
    printUnlocked(utoa(value, buffer, sizeof(buffer), 16), fg, bg);
}

void printDecUnlocked(const uint64_t value, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    char buffer[33];
    printUnlocked(utoa(value, buffer, sizeof(buffer)), fg, bg);
}

void setCursorUnlocked(const uint32_t x, const uint32_t y)
{
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot set cursor, framebuffer not initialized\n");
        return;
    }

    cursorX = x >= fbWidth / font.width ? fbWidth / font.width - 1 : x;
    cursorY = y >= fbHeight / font.height ? fbHeight / font.height - 1 : y;
}

void Renderer::init()
{
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
    {
        Serial::printf("Renderer: No framebuffer available\n");
        return;
    }

    const auto* fb = framebuffer_request.response->framebuffers[0];
    if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || fb->bpp != 32)
    {
        Serial::printf("Renderer: Unsupported framebuffer format (memory model: %u, bpp: %u)\n", fb->memory_model,
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
        Serial::printf("Renderer: Invalid PSF1 font header\n");
        fbAddress = nullptr;

        return;
    }

    font.glyphBuffer = asset_src_assets_fonts_zap_ext_light18_psf_start + sizeof(PSF1Header);
    font.width = 8;
    font.height = header->charSize;
    font.glyphCount = header->mode & 1 ? 512 : 256;
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
            case 1:
            case 7:
                swap(fg, bg);
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

void Renderer::scroll()
{
    LockGuard guard(renderLock);
    scrollUnlocked();
}

void Renderer::clear(const uint32_t color)
{
    LockGuard guard(renderLock);
    clearUnlocked(color);
}

void Renderer::printf(const char* fmt, ...)
{
    LockGuard guard(renderLock);
    if (!fbReady())
    {
        Serial::printf("Renderer: Cannot print, framebuffer not initialized\n");
        return;
    }
    if (!fmt || !*fmt)
    {
        Serial::printf("Renderer: Invalid format string\n");
        return;
    }

    AnsiState state;
    va_list args;
    va_start(args, fmt);
    vformat(fmt, args, [&state](const char c) { if (!processAnsi(state, c, ansiFg, ansiBg)) printCharUnlocked(c); },
            [](const char* s) { printUnlocked(s); },
            [](const uint64_t h) { printHexUnlocked(h); }, [](const uint64_t d) { printDecUnlocked(d); });
    va_end(args);
}

void Renderer::printChar(const char c, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    LockGuard guard(renderLock);
    printCharUnlocked(c, fg, bg);
}

void Renderer::printCharAt(const uint32_t x, const uint32_t y, const char c, const uint32_t fg = ansiFg,
                           const uint32_t bg = ansiBg)
{
    LockGuard guard(renderLock);
    if (!fbReady() || c == '\n' || c == '\r' || c == '\t')
    {
        Serial::printf("Renderer: Cannot print character, framebuffer not initialized or invalid character\n");
        return;
    }
    if (x >= fbWidth / font.width || y >= fbHeight / font.height)
    {
        Serial::printf("Renderer: Invalid position for character\n");
        return;
    }

    drawGlyph(x * font.width, y * font.height, c, fg, bg);
    if (serialPrint) Serial::printf("%c", c);
}

void Renderer::print(const char* str, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    LockGuard guard(renderLock);
    printUnlocked(str, fg, bg);
}

void Renderer::printAt(const uint32_t x, const uint32_t y, const char* str, const uint32_t fg = ansiFg,
                       const uint32_t bg = ansiBg)
{
    LockGuard guard(renderLock);

    setCursorUnlocked(x, y);
    printUnlocked(str, fg, bg);
}

void Renderer::printHex(const uint64_t value, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    char buffer[33];
    print(utoa(value, buffer, sizeof(buffer), 16), fg, bg);
}

void Renderer::printHexAt(const uint32_t x, const uint32_t y, const uint64_t value, const uint32_t fg,
                          const uint32_t bg)
{
    LockGuard guard(renderLock);

    setCursorUnlocked(x, y);
    printHexUnlocked(value, fg, bg);
}

void Renderer::printDec(const uint64_t value, const uint32_t fg = ansiFg, const uint32_t bg = ansiBg)
{
    char buffer[33];
    print(utoa(value, buffer, sizeof(buffer)), fg, bg);
}

void Renderer::printDecAt(const uint32_t x, const uint32_t y, const uint64_t value, const uint32_t fg,
                          const uint32_t bg)
{
    LockGuard guard(renderLock);

    setCursorUnlocked(x, y);
    printDecUnlocked(value, fg, bg);
}

void Renderer::setCursor(const uint32_t x, const uint32_t y)
{
    LockGuard guard(renderLock);
    setCursorUnlocked(x, y);
}

uint32_t Renderer::getCursorX() { return cursorX; }
uint32_t Renderer::getCursorY() { return cursorY; }
uint32_t Renderer::getFontWidth() { return font.width; }
uint32_t Renderer::getFontHeight() { return font.height; }

bool Renderer::getSerialPrint() { return serialPrint; }
void Renderer::setSerialPrint(const bool value) { serialPrint = value; }
