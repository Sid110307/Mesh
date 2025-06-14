ENTRY(_start)
SECTIONS {
    KERNEL_BASE = 0xFFFFFFFF80000000;
    . = KERNEL_BASE;
    _kernel_start = .;
    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data : ALIGN(4K) { *(.data .data.*) }
    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        __bss_end = .;
    }
    .assets : ALIGN(4K) {
        __assets_start = .;
        *(.assets .assets.*)
        __assets_end = .;
    }
    _kernel_end = .;
    /DISCARD/ : { *(.eh_frame) }
}
