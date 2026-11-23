ENTRY(_start)
SECTIONS {
    . = 0xFFFFFFFF80000000;
    .text : ALIGN(4K) {
        _text_start = .;
        KEEP(*(.text.boot))
        KEEP(*(.text.smp))
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
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
        *(.data .data.*)
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __data_end = .;
    }
    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment .note.*) }
}
