#include "./atomic.h"

Atomic::Atomic(const uint32_t init) : value(init)
{
}

uint32_t Atomic::load() const noexcept
{
	uint32_t val;
	asm volatile ("movl %1, %0" : "=r"(val) : "m"(value) : "memory");

	return val;
}

void Atomic::store(uint32_t val) noexcept { asm volatile ("movl %1, %0" : "=m"(value) : "r"(val) : "memory"); }

uint32_t Atomic::increment() noexcept
{
	uint32_t old_val = 0;
	asm volatile ("lock xaddl %0, %1" : "+r"(old_val), "+m"(value) :: "memory", "cc");

	return old_val + 1;
}

bool Atomic::compareExchange(uint32_t& expected, uint32_t desired) noexcept
{
	uint8_t success;
	uint32_t temp = expected;
	asm volatile ("lock cmpxchgl %3, %1\nsete %0" : "=q"(success), "+m"(value) : "a"(temp), "r"(desired) : "memory",
		"cc");

	if (!success) expected = load();
	return success;
}
