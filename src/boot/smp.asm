bits 64

section .text
    global trampoline
    extern apMain

trampoline:
    cli
    mov rsp, [rdi + 0]
    mov edi, [rdi + 8]
    call apMain
.hang:
    hlt
    jmp .hang
