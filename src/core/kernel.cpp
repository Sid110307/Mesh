#include "../drivers/renderer.h"
#include "../arch/gdt.h"
#include "../arch/idt.h"
#include "../memory/paging.h"
#include "./limine.h"

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
	Renderer::print("\x1b[32mMesh Booted Successfully!\n");

	if (framebuffer_request.response)
	{
		Renderer::print("\x1b[36mFramebuffer Info:\n");
		Renderer::print(" - Resolution: ");
		Renderer::printDec(framebuffer_request.response->framebuffers[0]->width);
		Renderer::print("x");
		Renderer::printDec(framebuffer_request.response->framebuffers[0]->height);
		Renderer::print("\n");
	}
	else
	{
		Renderer::print("\x1b[31mFramebuffer request failed!\n");
		Renderer::print(" - No framebuffer available.\n");
	}

	if (memory_request.response)
	{
		Renderer::print("\x1b[35mMemory Map:\n");
		Renderer::print(" - Entries: ");
		Renderer::printDec(memory_request.response->entry_count);
		Renderer::print("\n");
	}
	if (hhdm_request.response)
	{
		Renderer::print("\x1b[34mHHDM Base: ");
		Renderer::print("0x");
		Renderer::printHex(hhdm_request.response->offset);
		Renderer::print("\n");
	}
	if (kernel_addr_request.response)
	{
		Renderer::print("\x1b[33mKernel Address Range:\n");
		Renderer::print(" - Physical base: 0x");
		Renderer::printHex(kernel_addr_request.response->physical_base);
		Renderer::print("\n - Virtual base:  0x");
		Renderer::printHex(kernel_addr_request.response->virtual_base);
		Renderer::print("\n");
	}
	if (boot_time_request.response)
	{
		Renderer::print("\x1b[36mBoot Time (UNIX): ");
		Renderer::printDec(boot_time_request.response->boot_time);
		Renderer::print("\n");
	}
	if (smp_request.response && smp_request.response->cpu_count > 0)
	{
		Renderer::print("\x1b[32mSMP CPUs Detected: ");
		Renderer::printDec(smp_request.response->cpu_count);
		Renderer::print("\n");
	}
}

void initGDT()
{
	Renderer::print("\x1b[36mInitializing GDT... ");
	static uint8_t kernelStack[8192] __attribute__((aligned(16)));

	GDTManager::init();
	GDTManager::load();
	GDTManager::setTSS(reinterpret_cast<uint64_t>(kernelStack + sizeof(kernelStack)));
	Renderer::print("\x1b[32mDone!\n");
}

void initIDT()
{
	Renderer::print("\x1b[36mInitializing IDT... ");
	IDTManager::init();
	Renderer::print("\x1b[32mDone!\n");
}

void initPaging()
{
	Renderer::print("\x1b[36mInitializing Paging... ");
	uint64_t base = 0, size = 0;

	for (size_t i = 0; i < memory_request.response->entry_count; ++i)
	{
		auto* entry = memory_request.response->entries[i];
		if (entry->type != LIMINE_MEMMAP_USABLE || entry->base < 0x100000) continue;

		uint64_t alignedBase = (entry->base + 0xFFF) & ~0xFFFULL;
		if (uint64_t alignedSize = entry->length - (alignedBase - entry->base); alignedSize >=
			FrameAllocator::FRAME_SIZE && alignedSize > size)
		{
			base = alignedBase;
			size = alignedSize;
		}
	}

	if (!base)
	{
		Renderer::print("\x1b[31mFailed to find usable memory!\n");
		while (true) asm volatile("hlt");
	}

	FrameAllocator::init(base, size);
	Paging::init();
	Renderer::print("\x1b[32mDone!\n");
}

extern "C" [[noreturn]] void kernelMain()
{
	initRenderer();
	initGDT();
	initIDT();
	initPaging();

	while (true) asm volatile ("hlt");
}
