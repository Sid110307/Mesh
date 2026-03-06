#include <arch/x86_64/isr.h>
#include <arch/x86_64/lapic.h>
#include <core/panic.h>
#include <drivers/keyboard.h>
#include <memory/vmm.h>

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

void showException(const Interrupt::Frame* frame, const uint64_t intNum, const uint64_t errorCode)
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
        asm volatile ("mov %%cr2, %0" : "=r"(cr2));

        if (const VMM::Region* region = VMM::findRegion(cr2))
            Panic::panicFrame(
                frame,
                "%s\nCR2: 0x%lx\nError code: 0x%lx (P: %lu W: %lu U: %lu RS: %lu IF: %lu)\nVMM Region: %s\nRegion Base: 0x%lx\nRegion Size: 0x%lx\nRegion Flags: 0x%lx\nCommitted: %s\nDirect Mapped: %s",
                exceptionName(intNum), cr2, errorCode, errorCode & 1ULL, errorCode & 2ULL, errorCode & 4ULL,
                errorCode & 8ULL, errorCode & 16ULL, VMM::regionTypeName(region->type), region->base, region->size,
                static_cast<uint64_t>(region->flags), region->committed ? "Yes" : "No",
                region->directMapped ? "Yes" : "No");
        else
            Panic::panicFrame(
                frame, "%s\nCR2: 0x%lx\nError code: 0x%lx (P: %lu W: %lu U: %lu RS: %lu IF: %lu)\nVMM Region: None",
                exceptionName(intNum), cr2, errorCode, errorCode & 1ULL, errorCode & 2ULL, errorCode & 4ULL,
                errorCode & 8ULL, errorCode & 16ULL);
    }
    else Panic::panicFrame(frame, "%s", exceptionName(intNum));
}

bool Interrupt::interruptsEnabled()
{
    uint64_t rflags;
    asm volatile ("pushfq\npopq %0" : "=r"(rflags) :: "memory");

    return (rflags & (1ULL << 9)) != 0;
}

void Interrupt::enableInterrupts() { asm volatile ("sti" ::: "memory"); }
void Interrupt::disableInterrupts() { asm volatile ("cli" ::: "memory"); }

extern "C" {
__attribute__ ((interrupt)) void isr0(const Interrupt::Frame* f) { showException(f, 0, 0); }
__attribute__ ((interrupt)) void isr1(const Interrupt::Frame* f) { showException(f, 1, 0); }
__attribute__ ((interrupt)) void isr2(const Interrupt::Frame* f) { showException(f, 2, 0); }
__attribute__ ((interrupt)) void isr3(const Interrupt::Frame* f) { showException(f, 3, 0); }
__attribute__ ((interrupt)) void isr4(const Interrupt::Frame* f) { showException(f, 4, 0); }
__attribute__ ((interrupt)) void isr5(const Interrupt::Frame* f) { showException(f, 5, 0); }
__attribute__ ((interrupt)) void isr6(const Interrupt::Frame* f) { showException(f, 6, 0); }
__attribute__ ((interrupt)) void isr7(const Interrupt::Frame* f) { showException(f, 7, 0); }
__attribute__ ((interrupt)) void isr8(const Interrupt::Frame* f, const uint64_t e) { showException(f, 8, e); }
__attribute__ ((interrupt)) void isr9(const Interrupt::Frame* f) { showException(f, 9, 0); }
__attribute__ ((interrupt)) void isr10(const Interrupt::Frame* f, const uint64_t e) { showException(f, 10, e); }
__attribute__ ((interrupt)) void isr11(const Interrupt::Frame* f, const uint64_t e) { showException(f, 11, e); }
__attribute__ ((interrupt)) void isr12(const Interrupt::Frame* f, const uint64_t e) { showException(f, 12, e); }
__attribute__ ((interrupt)) void isr13(const Interrupt::Frame* f, const uint64_t e) { showException(f, 13, e); }
__attribute__ ((interrupt)) void isr14(const Interrupt::Frame* f, const uint64_t e) { showException(f, 14, e); }
__attribute__ ((interrupt)) void isr15(const Interrupt::Frame* f) { showException(f, 15, 0); }
__attribute__ ((interrupt)) void isr16(const Interrupt::Frame* f) { showException(f, 16, 0); }
__attribute__ ((interrupt)) void isr17(const Interrupt::Frame* f, const uint64_t e) { showException(f, 17, e); }
__attribute__ ((interrupt)) void isr18(const Interrupt::Frame* f) { showException(f, 18, 0); }
__attribute__ ((interrupt)) void isr19(const Interrupt::Frame* f) { showException(f, 19, 0); }
__attribute__ ((interrupt)) void isr20(const Interrupt::Frame* f) { showException(f, 20, 0); }
__attribute__ ((interrupt)) void isr21(const Interrupt::Frame* f) { showException(f, 21, 0); }
__attribute__ ((interrupt)) void isr22(const Interrupt::Frame* f) { showException(f, 22, 0); }
__attribute__ ((interrupt)) void isr23(const Interrupt::Frame* f) { showException(f, 23, 0); }
__attribute__ ((interrupt)) void isr24(const Interrupt::Frame* f) { showException(f, 24, 0); }
__attribute__ ((interrupt)) void isr25(const Interrupt::Frame* f) { showException(f, 25, 0); }
__attribute__ ((interrupt)) void isr26(const Interrupt::Frame* f) { showException(f, 26, 0); }
__attribute__ ((interrupt)) void isr27(const Interrupt::Frame* f) { showException(f, 27, 0); }
__attribute__ ((interrupt)) void isr28(const Interrupt::Frame* f) { showException(f, 28, 0); }
__attribute__ ((interrupt)) void isr29(const Interrupt::Frame* f) { showException(f, 29, 0); }
__attribute__ ((interrupt)) void isr30(const Interrupt::Frame* f) { showException(f, 30, 0); }
__attribute__ ((interrupt)) void isr31(const Interrupt::Frame* f) { showException(f, 31, 0); }

__attribute__ ((interrupt)) void isrKeyboard(Interrupt::Frame*)
{
    Keyboard::irq();
    LAPIC::sendEOI();
}
}
