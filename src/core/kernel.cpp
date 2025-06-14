#include "../drivers/renderer.h"
#include "./limine.h"

extern limine_framebuffer_request framebuffer_request;
extern limine_memmap_request memory_request;
extern limine_hhdm_request hhdm_request;
extern limine_kernel_address_request kernel_addr_request;
extern limine_smp_request smp_request;
extern limine_boot_time_request boot_time_request;

extern "C" [[noreturn]] void kernelMain()
{
	Renderer::init();
	Renderer::clear(BLACK);
	Renderer::print("\x1b[32mMesh Booted Successfully!\n\n", GREEN, BLACK);

	if (framebuffer_request.response)
	{
		Renderer::print("\x1b[36mFramebuffer Info:\n", CYAN, BLACK);
		Renderer::print(" - Resolution: ", WHITE, BLACK);
		Renderer::print(itoa(framebuffer_request.response->framebuffers[0]->width), WHITE, BLACK);
		Renderer::print("x", WHITE, BLACK);
		Renderer::print(itoa(framebuffer_request.response->framebuffers[0]->height), WHITE, BLACK);
		Renderer::print("\n\n", WHITE, BLACK);
	}
	else
	{
		Renderer::print("\x1b[31mFramebuffer request failed!\n", RED, BLACK);
		Renderer::print(" - No framebuffer available.\n\n", WHITE, BLACK);
	}

	if (memory_request.response)
	{
		Renderer::print("\x1b[35mMemory Map:\n", MAGENTA, BLACK);
		Renderer::print(" - Entries: ", WHITE, BLACK);
		Renderer::print(itoa(memory_request.response->entry_count), WHITE, BLACK);
		Renderer::print("\n\n", WHITE, BLACK);
	}
	if (hhdm_request.response)
	{
		Renderer::print("\x1b[34mHHDM Base: 0x", BLUE, BLACK);
		Renderer::print(itoa(hhdm_request.response->offset), WHITE, BLACK);
		Renderer::print("\n\n", WHITE, BLACK);
	}
	if (kernel_addr_request.response)
	{
		Renderer::print("\x1b[33mKernel Address Range:\n", YELLOW, BLACK);
		Renderer::print(" - Physical base: 0x", WHITE, BLACK);
		Renderer::print(itoa(kernel_addr_request.response->physical_base), WHITE, BLACK);
		Renderer::print("\n - Virtual base:  0x", WHITE, BLACK);
		Renderer::print(itoa(kernel_addr_request.response->virtual_base), WHITE, BLACK);
		Renderer::print("\n\n", WHITE, BLACK);
	}
	if (boot_time_request.response)
	{
		Renderer::print("\x1b[36mBoot Time (UNIX): ", CYAN, BLACK);
		Renderer::print(itoa(boot_time_request.response->boot_time), WHITE, BLACK);
		Renderer::print("\n", WHITE, BLACK);
	}
	if (smp_request.response && smp_request.response->cpu_count > 0)
	{
		Renderer::print("\x1b[32mSMP CPUs Detected: ", GREEN, BLACK);
		Renderer::print(itoa(smp_request.response->cpu_count), WHITE, BLACK);
		Renderer::print("\n", WHITE, BLACK);
	}

	Renderer::print("\n\x1b[0mSystem ready.\n", WHITE, BLACK);
	while (true) __asm__ __volatile__("hlt");
}
