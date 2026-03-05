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
    uint8_t locked;
};

class LockGuard
{
public:
    explicit LockGuard(Spinlock& l, bool hasInterrupts = true);
    ~LockGuard();

private:
    uint64_t rflags = 0;
    Spinlock& lock;
    bool hasInterrupts;
};
