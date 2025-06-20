#pragma once

#include <core/utils.h>

struct InterruptFrame
{
	uint64_t rip;
	uint64_t cs;
	uint64_t rsp;
	uint64_t ss;
	uint64_t rflags;
};

extern "C"
{
	__attribute__((interrupt)) void isr0(InterruptFrame* frame);
	__attribute__((interrupt)) void isr1(InterruptFrame* frame);
	__attribute__((interrupt)) void isr2(InterruptFrame* frame);
	__attribute__((interrupt)) void isr3(InterruptFrame* frame);
	__attribute__((interrupt)) void isr4(InterruptFrame* frame);
	__attribute__((interrupt)) void isr5(InterruptFrame* frame);
	__attribute__((interrupt)) void isr6(InterruptFrame* frame);
	__attribute__((interrupt)) void isr7(InterruptFrame* frame);
	__attribute__((interrupt)) void isr8(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr9(InterruptFrame* frame);
	__attribute__((interrupt)) void isr10(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr11(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr12(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr13(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr14(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr15(InterruptFrame* frame);
	__attribute__((interrupt)) void isr16(InterruptFrame* frame);
	__attribute__((interrupt)) void isr17(InterruptFrame* frame, uint64_t error);
	__attribute__((interrupt)) void isr18(InterruptFrame* frame);
	__attribute__((interrupt)) void isr19(InterruptFrame* frame);
	__attribute__((interrupt)) void isr20(InterruptFrame* frame);
	__attribute__((interrupt)) void isr21(InterruptFrame* frame);
	__attribute__((interrupt)) void isr22(InterruptFrame* frame);
	__attribute__((interrupt)) void isr23(InterruptFrame* frame);
	__attribute__((interrupt)) void isr24(InterruptFrame* frame);
	__attribute__((interrupt)) void isr25(InterruptFrame* frame);
	__attribute__((interrupt)) void isr26(InterruptFrame* frame);
	__attribute__((interrupt)) void isr27(InterruptFrame* frame);
	__attribute__((interrupt)) void isr28(InterruptFrame* frame);
	__attribute__((interrupt)) void isr29(InterruptFrame* frame);
	__attribute__((interrupt)) void isr30(InterruptFrame* frame);
	__attribute__((interrupt)) void isr31(InterruptFrame* frame);
}
