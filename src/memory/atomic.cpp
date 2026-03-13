#include <core/utils.h>
#include <memory/atomic.h>

template <typename T>
Atomic<T>::Atomic(const T init) : value(init) {}

template <typename T>
T Atomic<T>::load() const noexcept { return __atomic_load_n(&value, __ATOMIC_ACQUIRE); }

template <typename T>
void Atomic<T>::store(const T val) noexcept { __atomic_store_n(&value, val, __ATOMIC_RELEASE); }

template <typename T>
T Atomic<T>::increment() noexcept { return __atomic_add_fetch(&value, 1, __ATOMIC_ACQ_REL); }

template <typename T>
bool Atomic<T>::compareExchange(T& expected, const T desired) noexcept
{
    return __atomic_compare_exchange_n(&value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

template class Atomic<uint32_t>;
template class Atomic<uint64_t>;
