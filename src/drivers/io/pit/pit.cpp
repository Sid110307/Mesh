#include <drivers/io/pit/pit.h>
#include <drivers/io/serial/serial.h>
#include <kernel/sync/atomic.h>

constexpr uint32_t PIT_BASE_FREQUENCY = 1193182;
constexpr uint16_t PIT_CHANNEL0 = 0x40, PIT_COMMAND = 0x43;

Atomic ticks{0};
uint32_t frequency = 0;

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

    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void PIT::irq() { ticks.increment(); }

void PIT::sleepMs(uint32_t ms)
{
    if (ms == 0 || frequency == 0) return;

    uint64_t startTicks = getTicks(), targetTicks = startTicks + (static_cast<uint64_t>(ms) * frequency + 999) / 1000;
    while (getTicks() < targetTicks) asm volatile("hlt");
}

uint64_t PIT::getTicks() { return ticks.load(); }
