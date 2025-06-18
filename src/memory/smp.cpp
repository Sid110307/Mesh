#include "./smp.h"

#include "../drivers/video/renderer.h"
#include "../drivers/io/io.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../boot/limine.h"

extern limine_smp_request smp_request;

alignas(16) static uint8_t apStacks[SMP::MAX_CPUS][8192];
static uint32_t lapicIDs[SMP::MAX_CPUS] = {}, cpuCount = 0;

extern "C" [[noreturn]] void apMain(void* arg)
{
	asm volatile("mov %0, %%rsp" :: "r"(arg));

	GDTManager gdt;
	GDTManager::load();
	IDTManager::init();
	gdt.setTSS(reinterpret_cast<uint64_t>(arg));

	uint32_t id = SMP::currentID();
	Serial::printf("[AP] Core with LAPIC ID %u is online\n", id);

	while (true) asm volatile("hlt");
}

void SMP::init()
{
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

		cpu->extra_argument = reinterpret_cast<uint64_t>(&apStacks[i][8192]);
		cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);
		Renderer::printf("\x1b[33m[AP] \x1b[93mQueued core #%u (LAPIC %u)\x1b[0m\n", i, cpu->lapic_id);
	}
}

uint32_t SMP::currentID()
{
	uint32_t eax = 1, ebx, ecx, edx;
	asm volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));

	return (ebx >> 24) & 0xFF;
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getLapicID(const uint32_t index)
{
	if (index >= cpuCount) return 0xFFFFFFFF;
	return lapicIDs[index];
}
