bits 64
default rel

section .text
    global contextSwitch
    global threadTrampoline

    extern taskTrampoline

contextSwitch:
    mov [rdi + 0], r15
    mov [rdi + 8], r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbx
    mov [rdi + 40], rbp
    mov [rdi + 48], rsp

    pushfq
    pop qword [rdi + 56]
    lea rax, [rel .resume]
    mov [rdi + 64], rax
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbx, [rsi + 32]
    mov rbp, [rsi + 40]
    mov rsp, [rsi + 48]

    push qword [rsi + 56]
    popfq
    jmp qword [rsi + 64]
.resume:
    ret
threadTrampoline:
    mov rdi, [rsp]
    call taskTrampoline

    extern taskExit
    call taskExit
.hang:
    hlt
    jmp .hang
