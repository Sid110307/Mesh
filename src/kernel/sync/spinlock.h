#pragma once

#include <kernel/core/utils.h>

class Spinlock
{
public:
    Spinlock();

    void lock();
    void unlock();
    bool tryLock();
    [[nodiscard]] bool isLocked() const;

private:
    uint8_t locked;
};

class LockGuard
{
public:
    explicit LockGuard(Spinlock& l);
    ~LockGuard();

    Spinlock& lock;
};

class LockGuardIRQ
{
public:
    explicit LockGuardIRQ(Spinlock& l);
    ~LockGuardIRQ();

private:
    uint64_t rflags = 0;
    Spinlock& lock;
};
