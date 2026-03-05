bits 64

section .text
    global trampoline
    extern apMain

trampoline:
    cli
    mov rax, [rdi + 24]
    mov rsp, [rax + 0]
    mov edi, [rax + 8]
    call apMain
.hang:
    hlt
    jmp .hang
