bits 64
default rel

section .text
    global _start
    extern kernelMain

_start:
    lea rsp, [rel stack_top]
    call kernelMain
.hang:
    hlt
    jmp .hang

section .bss
    align 16
stack_bottom:
    resb 16384
stack_top:
