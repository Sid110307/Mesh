#include "./isr.h"

#include <drivers/io.h>

#include "../drivers/renderer.h"

const char* exceptionMessages[32] = {
	"Divide by Zero", "Debug", "Non-maskable Interrupt", "Breakpoint", "Overflow", "Bound Range Exceeded",
	"Invalid Opcode", "Device Not Available", "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
	"Segment Not Present", "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
	"Floating-Point Error", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
	"Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

char buffer[32];

static void showException(uint64_t intNum, uint64_t errorCode)
{
	Renderer::print("\x1b[31mException Raised: ");
	Renderer::print(intNum < 32 ? exceptionMessages[intNum] : "Unknown");
	Renderer::print("\nInterrupt: ");
	Renderer::print(utoa(intNum, buffer, sizeof(buffer)));
	Renderer::print(" | Error Code: ");
	Renderer::print(utoa(errorCode, buffer, sizeof(buffer), 16));
	Renderer::print("\nSystem Halted.\x1b[0m\n");

	while (true) asm volatile("hlt");
}

#define ISR_NOERR(n) __attribute__((interrupt)) void isr##n(InterruptFrame*) { showException(n, 0); }
#define ISR_ERR(n) __attribute__((interrupt)) void isr##n(InterruptFrame*, uint64_t error) { showException(n, error); }

ISR_NOERR(0)
ISR_NOERR(1)
ISR_NOERR(2)
ISR_NOERR(3)
ISR_NOERR(4)
ISR_NOERR(5)
ISR_NOERR(6)
ISR_NOERR(7)
ISR_ERR(8)
ISR_NOERR(9)
ISR_ERR(10)
ISR_ERR(11)
ISR_ERR(12)
ISR_ERR(13)
ISR_ERR(14)
ISR_NOERR(15)
ISR_NOERR(16)
ISR_ERR(17)
ISR_NOERR(18)
ISR_NOERR(19)
ISR_NOERR(20)
ISR_NOERR(21)
ISR_NOERR(22)
ISR_NOERR(23)
ISR_NOERR(24)
ISR_NOERR(25)
ISR_NOERR(26)
ISR_NOERR(27)
ISR_NOERR(28)
ISR_NOERR(29)
ISR_NOERR(30)
ISR_NOERR(31)
