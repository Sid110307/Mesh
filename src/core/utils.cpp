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

const char* itoa(uint64_t value)
{
	static char buffer[17];
	int index = 16;
	buffer[index--] = '\0';

	if (value == 0) buffer[index--] = '0';
	else
		while (value > 0 && index >= 0)
		{
			const uint8_t digit = value & 0xF;

			buffer[index--] = static_cast<char>(digit < 10 ? '0' + digit : 'A' + digit - 10);
			value >>= 4;
		}

	return &buffer[index + 1];
}

char* strchr(const char* str, int c)
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
