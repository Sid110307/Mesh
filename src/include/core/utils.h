#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

enum Color
{
    BLACK = 0x000000,
    RED = 0xFF0000,
    GREEN = 0x00FF00,
    YELLOW = 0xFFFF00,
    BLUE = 0x0000FF,
    MAGENTA = 0xFF00FF,
    CYAN = 0x00FFFF,
    WHITE = 0xFFFFFF,
    LIGHT_BLACK = 0x808080,
    LIGHT_RED = 0xFF8080,
    LIGHT_GREEN = 0x80FF80,
    LIGHT_YELLOW = 0xFFFF80,
    LIGHT_BLUE = 0x8080FF,
    LIGHT_MAGENTA = 0xFF80FF,
    LIGHT_CYAN = 0x80FFFF,
    LIGHT_WHITE = 0xE0E0E0,
    DEFAULT = 0xE0E0E0,
};

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);

uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t val);

int atoi(const char* str);
char* utoa(uint64_t value, char* buffer, size_t bufferSize, uint8_t base = 10, bool uppercase = true);
const char* strchr(const char* str, int c);
char* strtok_r(char* str, const char* delim, char** savePtr);

void swap(uint32_t& a, uint32_t& b) noexcept;
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);

template <typename PutChar, typename PutStr, typename PutHex, typename PutDec>
void vformat(const char* fmt, va_list args, PutChar putc, PutStr puts, PutHex putHex, PutDec putDec)
{
    auto putSigned = [&](int64_t val)
    {
        if (val < 0)
        {
            putc('-');
            val = -val;
        }
        putDec(static_cast<uint64_t>(val));
    };
    auto putUnsigned = [&](const uint64_t val) { putDec(val); };

    for (size_t i = 0; fmt[i]; ++i)
    {
        if (fmt[i] != '%')
        {
            putc(fmt[i]);
            continue;
        }

        switch (const char spec = fmt[++i])
        {
            case '%':
                {
                    putc('%');
                    break;
                }
            case 'c':
                {
                    putc(static_cast<char>(va_arg(args, int)));
                    break;
                }
            case 's':
                {
                    const char* str = va_arg(args, const char*);
                    puts(str ? str : "(null)");

                    break;
                }
            case 'd':
            case 'i':
                {
                    putSigned(va_arg(args, int));
                    break;
                }
            case 'u':
                {
                    putUnsigned(va_arg(args, unsigned int));
                    break;
                }
            case 'x':
            case 'X':
                {
                    putHex(va_arg(args, unsigned int));
                    break;
                }
            case 'p':
                {
                    if (void* ptr = va_arg(args, void*)) putHex(reinterpret_cast<uint64_t>(ptr));
                    else puts("(null)");

                    break;
                }
            case 'l':
                {
                    if (const char next = fmt[++i]; next == 'd' || next == 'i') putSigned(va_arg(args, long));
                    else if (next == 'u') putUnsigned(va_arg(args, unsigned long));
                    else if (next == 'x' || next == 'X') putHex(va_arg(args, unsigned long));
                    else
                    {
                        putc('%');
                        putc(next);
                    }

                    break;
                }
            case 'z':
                {
                    if (const char next = fmt[++i]; next == 'd' || next == 'i') putSigned(va_arg(args, intptr_t));
                    else if (next == 'u') putUnsigned(va_arg(args, size_t));
                    else if (next == 'x' || next == 'X') putHex(va_arg(args, size_t));
                    else
                    {
                        putc('%');
                        putc(next);
                    }

                    break;
                }
            default:
                putc('%');
                putc(spec);

                break;
        }
    }
}
