bits 64

section .text
    global trampoline
    extern apMain
    extern gdtPointer

trampoline:
    cli
    lgdt [gdtPointer]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, rdi
    call apMain
.hang:
    hlt
    jmp .hang
