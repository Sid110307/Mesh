#pragma once

#include <core/utils.h>

class Spinlock
{
public:
	Spinlock();

	void lock();
	void unlock();
	bool tryLock();
	bool isLocked() const;

private:
	volatile uint32_t locked;
};

class LockGuard
{
public:
	explicit LockGuard(Spinlock& l);
	~LockGuard();

	Spinlock& lock;
};
