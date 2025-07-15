#include <drivers/io/serial/serial.h>

Spinlock Serial::serialLock;

void Serial::init()
{
	if (initialized) return;

	outb(port + 1, 0x00);
	outb(port + 3, 0x80);
	outb(port + 0, 0x03);
	outb(port + 1, 0x00);
	outb(port + 3, 0x03);
	outb(port + 2, 0xC7);
	outb(port + 4, 0x0B);

	initialized = true;
}

void Serial::printf(const char* fmt, ...)
{
	LockGuard guard(serialLock);

	if (!initialized) init();
	if (!fmt || !*fmt)
	{
		printUnlocked("Serial: Invalid format string.\n");
		return;
	}

	va_list args;
	va_start(args, fmt);
	vformat(fmt, args, [](const char c) { printCharUnlocked(c); }, [](const char* s) { printUnlocked(s); },
	        [](const uint64_t h) { printHexUnlocked(h); }, [](const uint64_t d) { printDecUnlocked(d); });
	va_end(args);
}

void Serial::printChar(const uint8_t c)
{
	LockGuard guard(serialLock);

	if (!initialized) init();
	printCharUnlocked(c);
}

void Serial::print(const char* str)
{
	LockGuard guard(serialLock);
	printUnlocked(str);
}

void Serial::printHex(const uint64_t value)
{
	LockGuard guard(serialLock);
	printHexUnlocked(value);
}

void Serial::printDec(const uint64_t value)
{
	LockGuard guard(serialLock);
	printDecUnlocked(value);
}

void Serial::printCharUnlocked(const uint8_t c)
{
	int timeout = 1000000;
	while (!(inb(port + 5) & 0x20) && timeout--) asm volatile ("pause");

	outb(port, c);
}

void Serial::printUnlocked(const char* str)
{
	if (!initialized) init();
	if (!str)
	{
		printUnlocked("Serial: Invalid string.\n");
		return;
	}

	for (size_t i = 0; str[i] != '\0'; ++i) printCharUnlocked(static_cast<uint8_t>(str[i]));
}

void Serial::printHexUnlocked(const uint64_t value)
{
	char buffer[33];
	for (const char* p = utoa(value, buffer, sizeof(buffer), 16); *p; ++p) printCharUnlocked(*p);
}

void Serial::printDecUnlocked(const uint64_t value)
{
	char buffer[33];
	for (const char* p = utoa(value, buffer, sizeof(buffer)); *p; ++p) printCharUnlocked(*p);
}
