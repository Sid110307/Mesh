#include "../drivers/renderer.h"
#include "../arch/gdt.h"
#include "../arch/idt.h"
#include "./limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memory_request;
extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;
extern limine_boot_time_request boot_time_request;

void initRenderer()
{
	static char buffer[33] = {};

	Renderer::init();
	Renderer::clear(BLACK);
	Renderer::print("\x1b[32mMesh Booted Successfully!\n");

	if (framebuffer_request.response)
	{
		Renderer::print("\x1b[36mFramebuffer Info:\n");
		Renderer::print(" - Resolution: ");
		Renderer::print(utoa(framebuffer_request.response->framebuffers[0]->width, buffer, sizeof(buffer)));
		Renderer::print("x");
		Renderer::print(utoa(framebuffer_request.response->framebuffers[0]->height, buffer, sizeof(buffer)));
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
		Renderer::print(utoa(memory_request.response->entry_count, buffer, sizeof(buffer)));
		Renderer::print("\n");
	}
	if (hhdm_request.response)
	{
		Renderer::print("\x1b[34mHHDM Base: ");
		Renderer::print("0x");
		Renderer::print(utoa(hhdm_request.response->offset, buffer, sizeof(buffer), 16));
		Renderer::print("\n");
	}
	if (kernel_addr_request.response)
	{
		Renderer::print("\x1b[33mKernel Address Range:\n");
		Renderer::print(" - Physical base: 0x");
		Renderer::print(utoa(kernel_addr_request.response->physical_base, buffer, sizeof(buffer), 16));
		Renderer::print("\n - Virtual base:  0x");
		Renderer::print(utoa(kernel_addr_request.response->virtual_base, buffer, sizeof(buffer), 16));
		Renderer::print("\n");
	}
	if (boot_time_request.response)
	{
		Renderer::print("\x1b[36mBoot Time (UNIX): ");
		Renderer::print(utoa(boot_time_request.response->boot_time, buffer, sizeof(buffer)));
		Renderer::print("\n");
	}
	if (smp_request.response && smp_request.response->cpu_count > 0)
	{
		Renderer::print("\x1b[32mSMP CPUs Detected: ");
		Renderer::print(utoa(smp_request.response->cpu_count, buffer, sizeof(buffer)));
		Renderer::print("\n");
	}
}

void initGDT()
{
	Renderer::print("\x1b[36mInitializing GDT... ");
	GDTManager::init();
	GDTManager::load();
	GDTManager::setTSS(0x800000); // TODO: Set proper TSS
	Renderer::print("\x1b[32mDone!\n");
}

void initIDT()
{
	Renderer::print("\x1b[36mInitializing IDT... ");
	IDTManager::init();
	Renderer::print("\x1b[32mDone!\n");
}

extern "C" [[noreturn]] void kernelMain()
{
	initRenderer();
	initGDT();
	initIDT();

	while (true) asm volatile ("hlt");
}
