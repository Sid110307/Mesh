#include <memory/smp.h>
#include <memory/paging.h>
#include <drivers/video/renderer.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <boot/limine.h>

extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;

extern "C" void trampoline();
Spinlock SMP::smpLock;

alignas(4096) static uint8_t kernelStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
alignas(4096) static uint8_t apStacks[SMP::MAX_CPUS][SMP::SMP_STACK_SIZE];
static uint32_t lapicIDs[SMP::MAX_CPUS] = {};
static uint32_t apCount = 0;

Atomic SMP::apReadyCount{0};
uint32_t SMP::cpuCount = 0;
uint32_t cpuIDs[SMP::MAX_CPUS] = {};

extern "C" [[noreturn]] void apMain()
{
	uint64_t cr3 = reinterpret_cast<uint64_t>(pml4) - hhdm_request.response->offset;
	asm volatile("" ::: "memory");
	asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");

	uint32_t id = SMP::getCpuID();
	cpuIDs[id] = id;

	GDTManager gdt;
	GDTManager::load();
	IDTManager::init();
	GDTManager::setTSS(id, reinterpret_cast<uint64_t>(&kernelStacks[id][SMP::SMP_STACK_SIZE]));

	SMP::apReadyCount.increment();
	Renderer::printf("[AP] Core with LAPIC ID %u is online (CPU ID %u)\n", SMP::getLapicID(), id);

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

	lapicIDs[0] = smp_request.response->bsp_lapic_id;
	apCount = 0;

	for (uint32_t i = 0; i < cpuCount; ++i)
	{
		auto* cpu = smp_request.response->cpus[i];
		if (!cpu) continue;

		if (cpu->lapic_id == smp_request.response->bsp_lapic_id) continue;
		if (apCount >= MAX_CPUS - 1)
		{
			Renderer::printf("\x1b[31m[SMP] \x1b[91mToo many CPUs, ignoring core with LAPIC ID %u\x1b[0m\n",
			                 cpu->lapic_id);
			continue;
		}

		apCount++;
		lapicIDs[apCount] = cpu->lapic_id;
		GDTManager::setTSS(apCount, reinterpret_cast<uint64_t>(&kernelStacks[apCount][SMP_STACK_SIZE]));

		const uint64_t stackVirt = reinterpret_cast<uint64_t>(&apStacks[apCount][0]), stackPhys = stackVirt -
			               kernel_addr_request.response->virtual_base + kernel_addr_request.response->physical_base;
		for (size_t offset = 0; offset < SMP_STACK_SIZE; offset += FrameAllocator::SMALL_SIZE)
			if (!Paging::mapSmall(stackVirt + offset, stackPhys + offset, PageFlags::PRESENT | PageFlags::RW))
			{
				Renderer::printf("\x1b[31m[SMP] \x1b[91mFailed to map AP stack page at offset %zu!\x1b[0m\n", offset);
				return;
			}

		cpu->extra_argument = (stackVirt + SMP_STACK_SIZE) & ~0xFULL;
		cpu->goto_address = reinterpret_cast<uint8_t*>(trampoline);
		Renderer::printf("\x1b[33m[AP] \x1b[93mQueued core (LAPIC ID %u) as CPU ID %u\x1b[0m\n", cpu->lapic_id,
		                 apCount);
	}

	cpuCount = apCount + 1;
	waitForAPs();
}

uint32_t SMP::getCpuCount() { return cpuCount; }

uint32_t SMP::getCpuID()
{
	const uint32_t lapic = getLapicID();
	for (uint32_t i = 0; i < cpuCount; ++i) if (lapicIDs[i] == lapic) return i;

	Renderer::printf("[SMP] Failed to find LAPIC ID %u in CPU list!\n", lapic);
	Renderer::printf("[SMP] Available LAPIC IDs: ");
	for (uint32_t i = 0; i < cpuCount; ++i) Renderer::printf("%u ", lapicIDs[i]);
	Renderer::printf("\n");

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
	uint32_t expectedAPs = cpuCount - 1;

	Renderer::printf("\x1b[36m[SMP] \x1b[96mWaiting for %u APs to come online...\x1b[0m\n", expectedAPs);
	while (apReadyCount.load() < expectedAPs && timeout--) asm volatile ("pause");

	if (timeout <= 0)
	{
		Renderer::printf("\x1b[31mTimeout waiting for APs! Using %u cores instead of %u.\x1b[0m\n",
		                 apReadyCount.load() + 1, cpuCount);
		cpuCount = apReadyCount.load() + 1;
	}
	else Renderer::printf("\x1b[32mAll %u cores online!\x1b[0m\n", cpuCount);
}
