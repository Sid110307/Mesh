#include "./limine.h"

LIMINE_PTR(struct limine_framebuffer_request) framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};
LIMINE_PTR(struct limine_memmap_request) memory_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};
LIMINE_PTR(struct limine_hhdm_request) hhdm_request = {.id = LIMINE_HHDM_REQUEST, .revision = 0};
LIMINE_PTR(struct limine_kernel_address_request) kernel_addr_request = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0
};
LIMINE_PTR(struct limine_smp_request) smp_request = {.id = LIMINE_SMP_REQUEST, .revision = 0};
LIMINE_PTR(struct limine_boot_time_request) boot_time_request = {.id = LIMINE_BOOT_TIME_REQUEST, .revision = 0};
