#include <arch/x86_64/irq.h>
#include <memory/spinlock.h>

Spinlock::Spinlock() : locked(0) {}

void Spinlock::lock() { while (__atomic_test_and_set(&locked, __ATOMIC_ACQUIRE)) asm volatile ("pause"); }
void Spinlock::unlock() { __atomic_clear(&locked, __ATOMIC_RELEASE); }

bool Spinlock::tryLock()
{
    if (__atomic_test_and_set(&locked, __ATOMIC_ACQUIRE)) return false;
    return true;
}

bool Spinlock::isLocked() const { return __atomic_load_n(&locked, __ATOMIC_RELAXED); }

LockGuard::LockGuard(Spinlock& l) : lock(l) { lock.lock(); }
LockGuard::~LockGuard() { lock.unlock(); }

LockGuardIRQ::LockGuardIRQ(Spinlock& l) : lock(l)
{
    asm volatile ("pushfq\npopq %0" : "=r"(rflags) :: "memory");
    IRQ::disableInterrupts();

    lock.lock();
}

LockGuardIRQ::~LockGuardIRQ()
{
    lock.unlock();
    asm volatile("pushq %0\npopfq" :: "r"(rflags) : "memory");
}
