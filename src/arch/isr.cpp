#include <arch/isr.h>
#include <arch/lapic.h>
#include <core/panic.h>
#include <drivers/keyboard.h>

const char* exceptionName(const uint64_t intNum)
{
    switch (intNum)
    {
        case 0: return "Divide Error (#DE)";
        case 1: return "Debug (#DB)";
        case 2: return "Non-Maskable Interrupt (#NMI)";
        case 3: return "Breakpoint (#BP)";
        case 4: return "Overflow (#OF)";
        case 5: return "BOUND Range Exceeded (#BR)";
        case 6: return "Invalid Opcode (#UD)";
        case 7: return "Device Not Available (#NM)";
        case 8: return "Double Fault (#DF)";
        case 9: return "Coprocessor Segment Overrun (#CSO)";
        case 10: return "Invalid TSS (#TS)";
        case 11: return "Segment Not Present (#NP)";
        case 12: return "Stack-Segment Fault (#SS)";
        case 13: return "General Protection (#GP)";
        case 14: return "Page Fault (#PF)";
        case 16: return "x87 FP Exception (#MF)";
        case 17: return "Alignment Check (#AC)";
        case 18: return "Machine Check (#MC)";
        case 19: return "SIMD FP Exception (#XM/#XF)";
        case 20: return "Virtualization Exception (#VE)";
        case 21: return "Control Protection Exception (#CP)";
        default: return "Reserved/Unknown Exception";
    }
}

void showException(const InterruptFrame* frame, const uint64_t intNum, const uint64_t errorCode)
{
    if (intNum == 13)
    {
        const char* table = (errorCode & 0b11) == 0
                                ? "GDT"
                                : (errorCode & 0b11) == 1
                                ? "IDT"
                                : (errorCode & 0b11) == 2
                                ? "LDT"
                                : "Unknown";
        Panic::panicFrame(frame, "%s\nError code: 0x%lx (index: %lu, table: %s, ext: %lu)", exceptionName(intNum),
                          errorCode, errorCode >> 3, table, errorCode & 1ULL);
    }
    else if (intNum == 14)
    {
        uint64_t cr2 = 0;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));

        Panic::panicFrame(frame, "%s\nCR2: 0x%lx\nError code: 0x%lx (P: %lu W: %lu U: %lu RS: %lu IF: %lu)",
                          exceptionName(intNum), cr2, errorCode, (errorCode & 1ULL) != 0, (errorCode & 2ULL) != 0,
                          (errorCode & 4ULL) != 0, (errorCode & 8ULL) != 0, (errorCode & 16ULL) != 0);
    }
    else Panic::panicFrame(frame, "%s", exceptionName(intNum));
}

extern "C" {
__attribute__ ((interrupt)) void isr0(InterruptFrame* frame) { showException(frame, 0, 0); }
__attribute__ ((interrupt)) void isr1(InterruptFrame* frame) { showException(frame, 1, 0); }
__attribute__ ((interrupt)) void isr2(InterruptFrame* frame) { showException(frame, 2, 0); }
__attribute__ ((interrupt)) void isr3(InterruptFrame* frame) { showException(frame, 3, 0); }
__attribute__ ((interrupt)) void isr4(InterruptFrame* frame) { showException(frame, 4, 0); }
__attribute__ ((interrupt)) void isr5(InterruptFrame* frame) { showException(frame, 5, 0); }
__attribute__ ((interrupt)) void isr6(InterruptFrame* frame) { showException(frame, 6, 0); }
__attribute__ ((interrupt)) void isr7(InterruptFrame* frame) { showException(frame, 7, 0); }
__attribute__ ((interrupt)) void isr8(InterruptFrame* frame, const uint64_t error) { showException(frame, 8, error); }
__attribute__ ((interrupt)) void isr9(InterruptFrame* frame) { showException(frame, 9, 0); }
__attribute__ ((interrupt)) void isr10(InterruptFrame* frame, const uint64_t error) { showException(frame, 10, error); }
__attribute__ ((interrupt)) void isr11(InterruptFrame* frame, const uint64_t error) { showException(frame, 11, error); }
__attribute__ ((interrupt)) void isr12(InterruptFrame* frame, const uint64_t error) { showException(frame, 12, error); }
__attribute__ ((interrupt)) void isr13(InterruptFrame* frame, const uint64_t error) { showException(frame, 13, error); }
__attribute__ ((interrupt)) void isr14(InterruptFrame* frame, const uint64_t error) { showException(frame, 14, error); }
__attribute__ ((interrupt)) void isr15(InterruptFrame* frame) { showException(frame, 15, 0); }
__attribute__ ((interrupt)) void isr16(InterruptFrame* frame) { showException(frame, 16, 0); }
__attribute__ ((interrupt)) void isr17(InterruptFrame* frame, const uint64_t error) { showException(frame, 17, error); }
__attribute__ ((interrupt)) void isr18(InterruptFrame* frame) { showException(frame, 18, 0); }
__attribute__ ((interrupt)) void isr19(InterruptFrame* frame) { showException(frame, 19, 0); }
__attribute__ ((interrupt)) void isr20(InterruptFrame* frame) { showException(frame, 20, 0); }
__attribute__ ((interrupt)) void isr21(InterruptFrame* frame) { showException(frame, 21, 0); }
__attribute__ ((interrupt)) void isr22(InterruptFrame* frame) { showException(frame, 22, 0); }
__attribute__ ((interrupt)) void isr23(InterruptFrame* frame) { showException(frame, 23, 0); }
__attribute__ ((interrupt)) void isr24(InterruptFrame* frame) { showException(frame, 24, 0); }
__attribute__ ((interrupt)) void isr25(InterruptFrame* frame) { showException(frame, 25, 0); }
__attribute__ ((interrupt)) void isr26(InterruptFrame* frame) { showException(frame, 26, 0); }
__attribute__ ((interrupt)) void isr27(InterruptFrame* frame) { showException(frame, 27, 0); }
__attribute__ ((interrupt)) void isr28(InterruptFrame* frame) { showException(frame, 28, 0); }
__attribute__ ((interrupt)) void isr29(InterruptFrame* frame) { showException(frame, 29, 0); }
__attribute__ ((interrupt)) void isr30(InterruptFrame* frame) { showException(frame, 30, 0); }
__attribute__ ((interrupt)) void isr31(InterruptFrame* frame) { showException(frame, 31, 0); }

__attribute__ ((interrupt)) void isrKeyboard(InterruptFrame*)
{
    Keyboard::irq();
    LAPIC::sendEOI();
}

__attribute__ ((interrupt)) void isrTimer(InterruptFrame*)
{
    LAPIC::timerIrq();
    LAPIC::sendEOI();
}
}
