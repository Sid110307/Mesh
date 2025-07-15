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
	SMP::apReadyCount.increment();
	Renderer::printf("\x1b[33m[AP] Core %u online!\x1b[0m\n", SMP::getCpuID());
	
	asm volatile ("sti");
	while (true) asm volatile ("hlt");
}

void SMP::init()
{
	LockGuard guard(smpLock);

	if (!smp_request.response || smp_request.response->cpu_count == 0) return;
	cpuCount = smp_request.response->cpu_count;
	
	uint32_t low, high;
	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
	uint64_t lapicPhysBase = (static_cast<uint64_t>(high) << 32) | low;
	lapicPhysBase &= 0xFFFFF000;

	uint64_t lapicVirtBase = lapicPhysBase + hhdm_request.response->offset;
	if (!Paging::mapSmall(lapicVirtBase, lapicPhysBase, PageFlags::PRESENT | PageFlags::RW))
	{
		Renderer::printf("\x1b[31m[SMP] Failed to map LAPIC MMIO!\x1b[0m\n");
		return;
	}

	const uint64_t trampolineVirt = reinterpret_cast<uint64_t>(&trampoline);
	const uint64_t trampolinePhys = trampolineVirt -
		kernel_addr_request.response->virtual_base + kernel_addr_request.response->physical_base;

	if (!Paging::mapSmall(trampolineVirt, trampolinePhys, PageFlags::PRESENT | PageFlags::RW))
	{
		Renderer::printf("\x1b[31m[SMP] Failed to map trampoline!\x1b[0m\n");
		return;
	}

	uint32_t logicalID = 0;
	apCount = 0;

	for (uint32_t i = 0; i < cpuCount; ++i)
	{
		auto* cpu = smp_request.response->cpus[i];
		if (!cpu) continue;

		bool isBSP = cpu->lapic_id == smp_request.response->bsp_lapic_id;
		if (!isBSP && apCount + 1 >= MAX_CPUS)
		{
			Renderer::printf("\x1b[31m[SMP] Too many CPUs! Ignoring LAPIC ID %u\x1b[0m\n", cpu->lapic_id);
			continue;
		}

		lapicIDs[logicalID] = cpu->lapic_id;
		cpuIDs[logicalID] = logicalID;
		GDTManager::setTSS(logicalID, reinterpret_cast<uint64_t>(&kernelStacks[logicalID][SMP_STACK_SIZE]));

		if (!isBSP)
		{
			const uint64_t stackVirt = reinterpret_cast<uint64_t>(&apStacks[logicalID][0]);
			const uint64_t stackPhys = stackVirt -
				kernel_addr_request.response->virtual_base + kernel_addr_request.response->physical_base;

			if (SMP_STACK_SIZE == FrameAllocator::MEDIUM_SIZE && (stackVirt % FrameAllocator::MEDIUM_SIZE == 0) &&
				(stackPhys % FrameAllocator::MEDIUM_SIZE == 0))
				Paging::mapMedium(stackVirt, stackPhys, PageFlags::PRESENT | PageFlags::RW);
			else for (size_t offset = 0; offset < SMP_STACK_SIZE; offset += FrameAllocator::SMALL_SIZE)
				if (!Paging::mapSmall(stackVirt + offset, stackPhys + offset, PageFlags::PRESENT | PageFlags::RW))
				{
					Renderer::printf("\x1b[31m[SMP] Failed to map AP stack (offset %zu)\x1b[0m\n", offset);
					return;
				}

			cpu->extra_argument = (stackVirt + SMP_STACK_SIZE) & ~0xFULL;
			cpu->goto_address = reinterpret_cast<uint8_t*>(trampolineVirt);

			Renderer::printf("\x1b[33m[AP] Queued core (LAPIC ID %u) as CPU ID %u\x1b[0m\n", cpu->lapic_id, logicalID);
			apCount++;
		}

		logicalID++;
	}

	cpuCount = logicalID;
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
	return (low >> 24) & 0xFF;
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
