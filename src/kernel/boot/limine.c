#include <kernel/boot/limine.h>

__attribute__ ((used, section (".limine_requests"))) volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_executable_address_request
executable_addr_request =
{
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_date_at_boot_request date_at_boot_request =
{
    .id = LIMINE_DATE_AT_BOOT_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_executable_file_request
executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST, .revision = 0
};
__attribute__ ((used, section (".limine_requests"))) volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST, .revision = 0
};
