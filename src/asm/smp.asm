bits 64

section .text
    global trampoline
    extern apMain

trampoline:
    cli
    cld
    mov rax, [rdi + 24]
    mov rbx, rax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 11
    wrmsr
    mov rax, [rbx + 16]
    mov cr3, rax
    mov rax, rbx
    mov rsp, [rax + 0]
    mov edi, [rax + 8]
    call apMain
.hang:
    hlt
    jmp .hang
