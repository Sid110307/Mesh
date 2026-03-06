ENTRY(_start)
SECTIONS {
    KERNEL_BASE = 0xFFFFFFFF80000000;
    . = KERNEL_BASE;
    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
        KEEP(*(.text.smp))
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    }
    .rodata : ALIGN(4K) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }
    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }
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
    /DISCARD/ : { *(.eh_frame) }
}
