#pragma once

#include "../arch/common/atomic.h"
#include "../core/utils.h"

extern "C" void trampoline();

class SMP
{
public:
	static void init();
	static uint32_t getCpuCount();
	static uint32_t getLapicID();

	static constexpr size_t MAX_CPUS = 256, SMP_STACK_SIZE = 8192;
	static constexpr uintptr_t LAPIC_BASE = 0xFEE00000, LAPIC_ID_REGISTER_OFFSET = 0x20;
	static Atomic apReadyCount;

private:
	static void waitForAPs();
	static uint32_t cpuCount;
};
