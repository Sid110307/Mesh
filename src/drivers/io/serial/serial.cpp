#include "./serial.h"
#include "../../../arch/common/spinlock.h"

static Spinlock serialLock;

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
		write("Serial: Invalid format string.\n");
		return;
	}

	va_list args;
	va_start(args, fmt);
	vformat(fmt, args, [](const char c) { write(c); }, [](const char* s) { write(s); },
	        [](const uint64_t h) { writeHex(h); }, [](const uint64_t d) { writeDec(d); });
	va_end(args);
}

void Serial::write(const char* str)
{
	LockGuard guard(serialLock);

	if (!initialized) init();
	for (size_t i = 0; str[i] != '\0'; ++i) writeByte(static_cast<uint8_t>(str[i]));
}

void Serial::write(const char* str, const size_t len)
{
	LockGuard guard(serialLock);

	if (!initialized) init();
	for (size_t i = 0; i < len; ++i) writeByte(static_cast<uint8_t>(str[i]));
}

void Serial::write(const uint8_t byte) { write(reinterpret_cast<const char*>(&byte), sizeof(byte)); }

void Serial::writeHex(const uint64_t value)
{
	char buffer[33];
	write(utoa(value, buffer, sizeof(buffer), 16));
}

void Serial::writeDec(const uint64_t value)
{
	char buffer[33];
	write(utoa(value, buffer, sizeof(buffer)));
}

void Serial::writeByte(const uint8_t byte)
{
	int timeout = 100000;
	while (!(inb(port + 5) & 0x20) && timeout--)
	{
	}

	outb(port, byte);
}
