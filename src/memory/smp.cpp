#include "./smp.h"

#include "../drivers/video/renderer.h"
#include "../drivers/io/serial/serial.h"
#include "../arch/common/spinlock.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../boot/limine.h"

extern limine_smp_request smp_request;
static Spinlock smpLock;

alignas(16) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
static uint32_t lapicIDs[SMP::MAX_CPUS] = {};
Atomic SMP::apReadyCount{0};
uint32_t SMP::cpuCount = 0;

extern "C" [[noreturn]] void apMain(void* arg)
{
	asm volatile("mov %0, %%rsp" :: "r"(arg));

	GDTManager gdt;
	GDTManager::load();
	IDTManager::init();
	gdt.setTSS(reinterpret_cast<uint64_t>(arg));

	uint32_t id = SMP::getLapicID();
	Serial::printf("[AP] Core with LAPIC ID %u is online\n", id);
	SMP::apReadyCount.increment();

	while (true) asm volatile("hlt");
}

void SMP::init()
{
	LockGuard guard(smpLock);

	const auto* response = smp_request.response;
	if (!response || response->cpu_count == 0) return;

	cpuCount = response->cpu_count;
	Renderer::printf("\x1b[36m[SMP] \x1b[96m%u cores total\x1b[0m\n", cpuCount);

	for (uint32_t i = 0; i < cpuCount; ++i)
	{
		if (i >= MAX_CPUS)
		{
			Renderer::printf("\x1b[31m[SMP] \x1b[91mToo many CPUs, ignoring core #%u\x1b[0m\n", i);
			continue;
		}

		auto* cpu = response->cpus[i];
		if (!cpu)
		{
			Renderer::printf("\x1b[31m[SMP] \x1b[91mCore #%u is null, skipping...\x1b[0m\n", i);
			continue;
		}

		lapicIDs[i] = cpu->lapic_id;
		if (cpu->lapic_id == response->bsp_lapic_id) continue;

		cpu->extra_argument = reinterpret_cast<uint64_t>(&apStacks[i][SMP_STACK_SIZE]);
		cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);
		Renderer::printf("\x1b[33m[AP] \x1b[93mQueued core #%u (LAPIC %u)\x1b[0m\n", i, cpu->lapic_id);
	}

	waitForAPs();
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getLapicID()
{
	const uint32_t idReg = *reinterpret_cast<volatile uint32_t*>(LAPIC_BASE + LAPIC_ID_REGISTER_OFFSET);
	return (idReg >> 24) & 0xFF;
}

void SMP::waitForAPs()
{
	int timeout = 1000000;

	Renderer::printf("\x1b[36m[SMP] Waiting for APs to come online... ");
	while (apReadyCount.load() < cpuCount - 1 && timeout--) asm volatile("pause");

	if (timeout == 0)
	{
		Renderer::printf("\x1b[31m Timeout waiting for APs!\x1b[0m\n");
		while (true) asm volatile("hlt");
	}
	else Renderer::printf("\x1b[32m Ready.\x1b[0m\n");
}
