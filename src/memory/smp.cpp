#include "./smp.h"
#include "./paging.h"

#include "../drivers/video/renderer.h"
#include "../drivers/io/serial/serial.h"
#include "../arch/common/spinlock.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../boot/limine.h"

extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;

extern "C" void trampoline();

alignas(4096) static uint8_t kernelStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
alignas(4096) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
static uint32_t lapicIDs[SMP::MAX_CPUS] = {};
static Spinlock smpLock;

Atomic SMP::apReadyCount{0};
uint32_t SMP::cpuCount = 0;
uint32_t cpuIDs[SMP::MAX_CPUS] = {};

extern "C" [[noreturn]] void apMain()
{
	uint64_t cr3 = reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset;
	asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");

	uint32_t id = SMP::getCpuID();
	cpuIDs[id] = id;

	GDTManager gdt;
	GDTManager::load();
	IDTManager::init();
	GDTManager::setTSS(id, reinterpret_cast<uint64_t>(&kernelStacks[id][SMP::SMP_STACK_SIZE]));

	SMP::apReadyCount.increment();
	Serial::printf("[AP] Core with LAPIC ID %u is online\n", id);
	for (uint32_t i = 0; i < SMP::cpuCount; ++i)
	{
		Serial::printf("[AP] LAPIC ID #%u: %u\n", i, lapicIDs[i]);
		if (lapicIDs[i] == SMP::getLapicID())
		{
			Serial::printf("[AP] Core #%u is now online\n", i);
			break;
		}
	}

	asm volatile ("sti");
	while (true) asm volatile ("hlt");
}

void SMP::init()
{
	LockGuard guard(smpLock);

	if (!smp_request.response || smp_request.response->cpu_count == 0) return;
	cpuCount = smp_request.response->cpu_count;

	const auto trampolineVirt = reinterpret_cast<uint64_t>(&trampoline), trampolinePhys = trampolineVirt -
		           kernel_addr_request.response->virtual_base + kernel_addr_request.response->physical_base;
	if (!Paging::mapSmall(trampolinePhys, trampolinePhys, PageFlags::PRESENT | PageFlags::RW))
	{
		Renderer::printf("\x1b[31m[SMP] \x1b[91mFailed to map trampoline page!\x1b[0m\n");
		return;
	}

	for (uint32_t i = 0; i < cpuCount; ++i)
	{
		if (i >= MAX_CPUS)
		{
			Renderer::printf("\x1b[31m[SMP] \x1b[91mToo many CPUs, ignoring core #%u\x1b[0m\n", i);
			continue;
		}

		auto* cpu = smp_request.response->cpus[i];
		if (!cpu)
		{
			Renderer::printf("\x1b[31m[SMP] \x1b[91mCore #%u is null, skipping...\x1b[0m\n", i);
			continue;
		}

		lapicIDs[i] = cpu->lapic_id;
		if (cpu->lapic_id == smp_request.response->bsp_lapic_id) continue;
		GDTManager::setTSS(i, reinterpret_cast<uint64_t>(&kernelStacks[i][SMP_STACK_SIZE]));

		const uint64_t stackVirt = reinterpret_cast<uint64_t>(&apStacks[i][0]),
		               stackPhys = stackVirt - hhdm_request.response->offset;
		for (size_t offset = 0; offset < SMP_STACK_SIZE; offset += FrameAllocator::SMALL_SIZE)
			if (!Paging::mapSmall(stackVirt + offset, stackPhys + offset, PageFlags::PRESENT | PageFlags::RW))
			{
				Renderer::printf("\x1b[31m[SMP] \x1b[91mFailed to map AP stack page at offset %zu!\x1b[0m\n", offset);
				return;
			}

		cpu->extra_argument = stackVirt + SMP_STACK_SIZE;
		cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);
		Renderer::printf("\x1b[33m[AP] \x1b[93mQueued core #%u (LAPIC %u)\x1b[0m\n", i, cpu->lapic_id);
	}

	waitForAPs();
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getCpuID()
{
	const uint32_t lapic = getLapicID();
	for (uint32_t i = 0; i < cpuCount; ++i) if (lapicIDs[i] == lapic) return i;

	Serial::printf("[SMP] Failed to find LAPIC ID %u in CPU list!\n", lapic);
	while (true) asm volatile ("hlt");
}

uint32_t SMP::getLapicID()
{
	uint32_t low, high;
	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x802));

	return low;
}

void SMP::waitForAPs()
{
	int timeout = 1000000;

	Renderer::printf("\x1b[36m[SMP] \x1b[96mWaiting for APs to come online...\x1b[0m\n");
	while (apReadyCount.load() < cpuCount - 1 && timeout--) asm volatile ("pause");

	if (timeout <= 0)
	{
		Renderer::printf("\x1b[31mTimeout waiting for APs! Using %u cores.\x1b[0m\n", apReadyCount.load() + 1);
		cpuCount = apReadyCount.load() + 1;
	}
	else Renderer::printf("\x1b[32mReady.\x1b[0m\n");
}
