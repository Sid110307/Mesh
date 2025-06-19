bits 64

section .text
    global trampoline
    extern apMain

trampoline:
    cli
    mov rsp, rdi
    call apMain
.hang:
    hlt
    jmp .hang
