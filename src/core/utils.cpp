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
		result = result * 10 + (*str - '0');
		str++;
	}

	return negative ? -result : result;
}

char* utoa(uint64_t value, char* buffer, const size_t bufferSize, const uint8_t base, const bool uppercase)
{
	if (!buffer || bufferSize < 2 || base < 2 || base > 36) return nullptr;

	static constexpr auto digitsLower = "0123456789abcdefghijklmnopqrstuvwxyz";
	static constexpr auto digitsUpper = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char* digits = uppercase ? digitsUpper : digitsLower;

	int index = static_cast<int>(bufferSize - 1);
	buffer[index--] = '\0';

	if (value == 0)
	{
		if (index < 0) return nullptr;
		buffer[index--] = '0';
	}
	else
	{
		while (value > 0 && index >= 0)
		{
			buffer[index--] = digits[value % base];
			value /= base;
		}
	}

	return &buffer[index + 1];
}

char* strchr(const char* str, const int c)
{
	while (*str)
	{
		if (*str == static_cast<char>(c)) return const_cast<char*>(str);
		str++;
	}
	return nullptr;
}

char* strtok(char* str, const char* delim)
{
	static char* nextToken = nullptr;

	if (str) nextToken = str;
	if (!nextToken) return nullptr;

	while (*nextToken && strchr(delim, *nextToken)) ++nextToken;
	if (*nextToken == '\0') return nullptr;

	char* tokenStart = nextToken;
	while (*nextToken && !strchr(delim, *nextToken)) ++nextToken;

	if (*nextToken)
	{
		*nextToken = '\0';
		++nextToken;
	}

	return tokenStart;
}

void* memcpy(void* dest, const void* src, size_t n)
{
	if (!dest || !src || n == 0) return dest;

	auto d = static_cast<char*>(dest);
	auto s = static_cast<const char*>(src);

	if (n >= 4 && reinterpret_cast<uintptr_t>(d) % 4 == 0 && reinterpret_cast<uintptr_t>(s) % 4 == 0)
		while (n >= 4)
		{
			*reinterpret_cast<uint32_t*>(d) = *reinterpret_cast<const uint32_t*>(s);

			d += 4;
			s += 4;
			n -= 4;
		}

	while (n--) *d++ = *s++;
	return dest;
}

void* memset(void* dest, const int c, size_t n)
{
	if (!dest || n == 0) return dest;

	auto d = static_cast<char*>(dest);
	while (n--) *d++ = static_cast<char>(c);
	return dest;
}

void* memmove(void* dest, const void* src, size_t n)
{
	if (!dest || !src || n == 0 || dest == src) return dest;

	auto d = static_cast<char*>(dest);
	auto s = static_cast<const char*>(src);

	if (d < s || d >= s + n) while (n--) *d++ = *s++;
	else
	{
		d += n;
		s += n;

		while (n--) *--d = *--s;
	}

	return dest;
}

uint8_t inb(uint16_t port)
{
	uint8_t ret;
	asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outb(uint16_t port, uint8_t val) { asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
