#pragma once

#include <core/utils.h>

struct InterruptFrame
{
    uint64_t rip, cs, rflags, rsp, ss;
};

extern "C" {
__attribute__ ((interrupt)) void isr0(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr1(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr2(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr3(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr4(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr5(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr6(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr7(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr8(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr9(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr10(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr11(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr12(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr13(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr14(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr15(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr16(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr17(const InterruptFrame* frame, uint64_t error);
__attribute__ ((interrupt)) void isr18(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr19(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr20(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr21(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr22(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr23(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr24(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr25(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr26(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr27(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr28(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr29(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr30(const InterruptFrame* frame);
__attribute__ ((interrupt)) void isr31(const InterruptFrame* frame);

__attribute__ ((interrupt)) void isrKeyboard(InterruptFrame* frame);
__attribute__ ((interrupt)) void isrTimer(InterruptFrame* frame);
}
