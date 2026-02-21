#pragma once

#include <core/utils.h>

class Spinlock
{
public:
    Spinlock();

    void lock();
    void unlock();
    bool tryLock();
    [[nodiscard]] bool isLocked() const;

private:
    volatile uint8_t locked;
};

class LockGuard
{
public:
    explicit LockGuard(Spinlock& l);
    ~LockGuard();

    Spinlock& lock;
};
