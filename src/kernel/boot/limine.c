#include <kernel/boot/limine.h>

__attribute__ ((used, section (".limine_requests"))) volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_memmap_request memory_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_kernel_address_request kernel_addr_request =
{
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_boot_time_request boot_time_request = {
    .id = LIMINE_BOOT_TIME_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST, .revision = 0
};
