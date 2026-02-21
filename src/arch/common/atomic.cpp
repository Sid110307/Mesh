#include <arch/common/atomic.h>

Atomic::Atomic(const uint32_t init) : value(init)
{
}

uint32_t Atomic::load() const noexcept { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }
void Atomic::store(const uint32_t val) noexcept { __atomic_store_n(&value, val, __ATOMIC_RELEASE); }
uint32_t Atomic::increment() noexcept { return __atomic_add_fetch(&value, 1, __ATOMIC_ACQ_REL); }

bool Atomic::compareExchange(uint32_t& expected, const uint32_t desired) noexcept
{
    return __atomic_compare_exchange_n(&value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
