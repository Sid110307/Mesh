section .text
    global isr0
    global isr8
    global isr14

isr0:
    cli
    hlt
    jmp isr0
isr8:
    cli
    hlt
    jmp isr8
isr14:
    cli
    hlt
    jmp isr14
