#include "./io.h"

uint8_t inb(uint16_t port)
{
	uint8_t ret;
	asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

void outb(uint16_t port, uint8_t val) { asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }

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

void Serial::write(const char* str)
{
	if (!initialized) init();
	for (size_t i = 0; str[i] != '\0'; ++i) writeByte(static_cast<uint8_t>(str[i]));
}

void Serial::write(const uint8_t byte) { write(reinterpret_cast<const char*>(&byte), sizeof(byte)); }

void Serial::write(const char* str, const size_t len)
{
	if (!initialized) init();
	for (size_t i = 0; i < len; ++i) writeByte(static_cast<uint8_t>(str[i]));
}

void Serial::writeByte(const uint8_t byte)
{
	while (!(inb(port + 5) & 0x20))
	{
	}
	outb(port, byte);
}
