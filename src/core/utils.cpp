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

	size_t index = bufferSize - 1;
	buffer[index--] = '\0';

	if (value == 0)
	{
		if (index == SIZE_MAX && value) return nullptr;
		buffer[index--] = '0';
	}
	else
		while (value > 0 && index != SIZE_MAX)
		{
			buffer[index--] = digits[value % base];
			value /= base;
		}

	return &buffer[index + 1];
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

	auto d = static_cast<char*>(dest);
	auto s = static_cast<const char*>(src);

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

	auto d = static_cast<char*>(dest);
	while (n--) *d++ = static_cast<char>(c);
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
