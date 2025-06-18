#pragma once

#include "../core/utils.h"

extern "C" void trampoline();

class SMP
{
public:
	static void init();
	static uint32_t currentID();
	static uint32_t getCpuCount();
	static uint32_t getLapicID(uint32_t index);

	static constexpr size_t MAX_CPUS = 256;
};
