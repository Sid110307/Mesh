#include "./utils.h"

int atoi(const char* str)
{
	int result = 0;
	bool negative = false;

	while (*str == ' ' || *str == '\t') str++;
	if (*str == '-')
	{
		negative = true;
		str++;
	}
	else if (*str == '+') str++;

	while (*str >= '0' && *str <= '9')
	{
		const int digit = *str - '0';
		if (result > (INT_MAX - digit) / 10) return negative ? INT_MIN : INT_MAX;

		result = result * 10 + digit;
		++str;
	}
	return negative ? -result : result;
}

char* utoa(uint64_t value, char* buffer, const size_t bufferSize, const uint8_t base, const bool uppercase)
{
	if (!buffer || bufferSize < 2 || base < 2 || base > 36) return nullptr;

	static constexpr auto digitsLower = "0123456789abcdefghijklmnopqrstuvwxyz";
	static constexpr auto digitsUpper = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char* digits = uppercase ? digitsUpper : digitsLower;

	size_t index = bufferSize;
	buffer[--index] = '\0';

	if (value == 0)
	{
		if (index == 0) return nullptr;
		buffer[--index] = '0';

		return &buffer[index];
	}

	while (value > 0 && index > 0)
	{
		buffer[--index] = digits[value % base];
		value /= base;
	}

	return index < bufferSize ? &buffer[index] : nullptr;
}

char* strchr(const char* str, const int c)
{
	while (*str)
	{
		if (const auto uc = static_cast<unsigned char>(*str); uc == static_cast<unsigned char>(c))
			return const_cast<char*>(str);
		str++;
	}
	return nullptr;
}

char* strtok_r(char* str, const char* delim, char** savePtr)
{
	if (str) *savePtr = str;
	if (!*savePtr) return nullptr;

	char* tokenStart = nullptr;

	while (**savePtr && strchr(delim, **savePtr)) ++(*savePtr);
	if (**savePtr == '\0') return nullptr;

	tokenStart = *savePtr;
	while (**savePtr && !strchr(delim, **savePtr)) ++(*savePtr);

	if (**savePtr)
	{
		**savePtr = '\0';
		++*savePtr;
	}

	return tokenStart;
}

void* memcpy(void* dest, const void* src, size_t n)
{
	if (!dest || !src || n == 0) return dest;

	auto* d = static_cast<uint8_t*>(dest);
	auto* s = static_cast<const uint8_t*>(src);

	if ((reinterpret_cast<uintptr_t>(d) | reinterpret_cast<uintptr_t>(s)) % 8 == 0)
		while (n >= 8)
		{
			*reinterpret_cast<uint64_t*>(d) = *reinterpret_cast<const uint64_t*>(s);

			d += 8;
			s += 8;
			n -= 8;
		}

	while (n--) *d++ = *s++;
	return dest;
}

void* memset(void* dest, const int c, size_t n)
{
	if (!dest || n == 0) return dest;

	const auto b = static_cast<uint8_t>(c);
	auto d = static_cast<uint8_t*>(dest);

	if (reinterpret_cast<uintptr_t>(d) % 8 == 0 && n >= 8)
	{
		uint64_t pattern = b;
		pattern |= pattern << 8;
		pattern |= pattern << 16;
		pattern |= pattern << 32;

		auto d64 = reinterpret_cast<uint64_t*>(d);
		while (n >= 8)
		{
			*d64++ = pattern;
			n -= 8;
		}
		d = reinterpret_cast<uint8_t*>(d64);
	}

	while (n--) *d++ = b;
	return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
	if (!dest || !src || n == 0 || dest == src) return dest;
	const auto dInt = reinterpret_cast<uintptr_t>(dest);

	if (const auto sInt = reinterpret_cast<uintptr_t>(src); dInt < sInt || dInt - sInt >= n)
	{
		auto dPtr = static_cast<char*>(dest);
		auto sPtr = static_cast<const char*>(src);

		while (n--) *dPtr++ = *sPtr++;
	}
	else
	{
		auto dPtr = static_cast<char*>(dest) + n;
		auto sPtr = static_cast<const char*>(src) + n;

		while (n--) *--dPtr = *--sPtr;
	}

	return dest;
}

void vformat(const char* fmt, va_list args, const putCharFn putc, const putStrFn puts, const putHexFn putHex,
             const putDecFn putDec)
{
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
				putc('%');
				break;
			case 'c':
				putc(static_cast<char>(va_arg(args, int)));
				break;
			case 's':
			{
				const char* str = va_arg(args, const char*);
				puts(str ? str : "(null)");

				break;
			}
			case 'd':
			case 'i':
			{
				int val = va_arg(args, int);
				if (val < 0)
				{
					putc('-');
					val = -val;
				}

				putDec(static_cast<uint64_t>(val));
				break;
			}
			case 'u':
				putDec(va_arg(args, uint32_t));
				break;
			case 'x':
			case 'X':
				putHex(va_arg(args, uint64_t));
				break;
			case 'p':
			{
				if (void* ptr = va_arg(args, void*)) putHex(reinterpret_cast<uint64_t>(ptr));
				else puts("(null)");

				break;
			}
			case 'l':
			{
				if (const char next = fmt[++i]; next == 'd' || next == 'i')
				{
					long val = va_arg(args, long);
					if (val < 0)
					{
						putc('-');
						val = -val;
					}

					putDec(static_cast<uint64_t>(val));
				}
				else if (next == 'u') putDec(va_arg(args, unsigned long));
				else if (next == 'x' || next == 'X') putHex(va_arg(args, unsigned long));
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
