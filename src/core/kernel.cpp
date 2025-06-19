#include "../drivers/video/renderer.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../memory/paging.h"
#include "../memory/smp.h"
#include "../boot/limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memory_request;
extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;
extern limine_boot_time_request boot_time_request;

void initRenderer()
{
	Renderer::init();
	Renderer::clear(BLACK);
	Renderer::printf("\x1b[92mMesh Booted Successfully!\n");
}

void dumpStats()
{
	if (framebuffer_request.response)
		Renderer::printf("\x1b[36m[Framebuffer] \x1b[96m%ux%u\x1b[0m\n",
		                 framebuffer_request.response->framebuffers[0]->width,
		                 framebuffer_request.response->framebuffers[0]->height);
	if (memory_request.response)
		Renderer::printf("\x1b[36m[Memory Map] \x1b[96m%u entries\x1b[0m\n", memory_request.response->entry_count);
	if (hhdm_request.response)
		Renderer::printf("\x1b[36m[HHDM Base] \x1b[96m0x%lx\x1b[0m\n", hhdm_request.response->offset);
	if (kernel_addr_request.response)
		Renderer::printf(
			"\x1b[36m[Kernel Range]\n \x1b[90m-\x1b[0m Physical: 0x%lx\n \x1b[90m-\x1b[0m Virtual: 0x%lx\n",
			kernel_addr_request.response->physical_base, kernel_addr_request.response->virtual_base);
	if (boot_time_request.response)
		Renderer::printf("\x1b[36m[Boot Time] \x1b[96m%lu\x1b[0m ms\n", boot_time_request.response->boot_time);
	if (smp_request.response && smp_request.response->cpu_count > 0)
		Renderer::printf("\x1b[36m[SMP] \x1b[96m%u CPUs Detected\x1b[0m\n", smp_request.response->cpu_count);
}

void initGDT()
{
	Renderer::printf("\x1b[36mInitializing GDT... ");
	static uint8_t kernelStack[SMP::SMP_STACK_SIZE] __attribute__((aligned(16)));

	[[maybe_unused]] GDTManager gdtManager;
	GDTManager::load();
	GDTManager::setTSS(0, reinterpret_cast<uint64_t>(&kernelStack[SMP::SMP_STACK_SIZE]));

	Renderer::printf("\x1b[32mDone!\n");
}

void initIDT()
{
	Renderer::printf("\x1b[36mInitializing IDT... ");
	IDTManager::init();
	Renderer::printf("\x1b[32mDone!\n");
}

void initPaging()
{
	Renderer::printf("\x1b[36mInitializing Paging... ");
	FrameAllocator::init();
	Paging::init();
	Renderer::printf("\x1b[32mDone!\n");
}

void initAPIC()
{
	constexpr uint32_t IA32_APIC_BASE_MSR = 0x1B;
	constexpr uintptr_t LAPIC_SVR_OFFSET = 0xF0;

	uint32_t apicLow, apicHigh;
	asm volatile ("rdmsr" : "=a"(apicLow), "=d"(apicHigh) : "c"(IA32_APIC_BASE_MSR));

	uint64_t base = (static_cast<uint64_t>(apicHigh) << 32) | apicLow;
	base |= (1ULL << 11);
	base = (base & ~(0xFFFFFULL << 12)) | (SMP::LAPIC_BASE & 0xFFFFF000ULL);
	asm volatile ("wrmsr" :: "c"(IA32_APIC_BASE_MSR), "a"(static_cast<uint32_t>(base & 0xFFFFFFFF)), "d"(static_cast<
		uint32_t>(base >> 32)));

	volatile uint32_t* svr = reinterpret_cast<uint32_t*>(SMP::LAPIC_BASE + LAPIC_SVR_OFFSET);
	uint32_t val = *svr;
	val |= 1 << 8;
	val = (val & 0xFFFFFF00) | 0xFF;
	*svr = val;
}

extern "C" [[noreturn]] void kernelMain()
{
	initRenderer();
	dumpStats();
	initPaging();
	initGDT();
	initIDT();
	initAPIC();
	SMP::init();

	while (true) asm volatile ("hlt");
}
