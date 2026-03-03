#include <drivers/io/pit/pit.h>
#include <drivers/io/serial/serial.h>

Atomic PIT::ticks{0};
uint32_t PIT::frequency = 0;

void PIT::init(const uint32_t freq)
{
    if (freq == 0 || freq > PIT_BASE_FREQUENCY)
    {
        Serial::printf("PIT: Invalid frequency %u Hz\n", freq);
        return;
    }

    frequency = freq;
    ticks.store(0);

    uint32_t divisor = PIT_BASE_FREQUENCY / freq;
    if (divisor < 1) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    setDivisor(divisor);
}

void PIT::irq() { ticks.increment(); }

void PIT::sleepMs(uint32_t ms)
{
    if (ms == 0 || frequency == 0) return;

    uint64_t startTicks = getTicks(), targetTicks = startTicks + (static_cast<uint64_t>(ms) * frequency + 999) / 1000;
    while (getTicks() < targetTicks) asm volatile("hlt");
}

uint64_t PIT::getTicks() { return ticks.load(); }

void PIT::setDivisor(const uint16_t divisor)
{
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}
