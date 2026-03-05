#pragma once

#include <core/utils.h>

namespace Interrupt
{
    struct Frame
    {
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rdx, rcx, rbx, rax, rip, cs, rflags, rsp, ss;
    };

    bool interruptsEnabled();
    void enableInterrupts();
    void disableInterrupts();
}

extern "C" {
__attribute__ ((interrupt)) void isr0(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr1(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr2(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr3(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr4(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr5(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr6(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr7(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr8(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr9(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr10(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr11(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr12(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr13(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr14(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr15(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr16(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr17(const Interrupt::Frame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr18(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr19(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr20(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr21(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr22(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr23(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr24(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr25(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr26(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr27(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr28(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr29(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr30(const Interrupt::Frame* frame);
__attribute__ ((interrupt)) void isr31(const Interrupt::Frame* frame);

__attribute__ ((interrupt)) void isrKeyboard(Interrupt::Frame* frame);
}
