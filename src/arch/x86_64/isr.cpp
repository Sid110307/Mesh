#include "./isr.h"

#include "../../memory/paging.h"
#include "../../drivers/video/renderer.h"

static void showException(InterruptFrame* frame, uint64_t intNum, uint64_t errorCode)
{
	Renderer::setSerialPrint(true);
	Renderer::printf("\x1b[31m\nException Raised: %lu\n", intNum);

	switch (intNum)
	{
		case 0:
		{
			Renderer::printf("Error: Division by zero.\n");
			break;
		}
		case 1:
		{
			Renderer::printf("Error: Debug exception.\n");
			break;
		}
		case 2:
		{
			Renderer::printf("Error: Non-maskable interrupt.\n");
			break;
		}
		case 3:
		{
			Renderer::printf("Error: Breakpoint exception.\n");
			break;
		}
		case 4:
		{
			Renderer::printf("Error: Overflow exception.\n");
			break;
		}
		case 5:
		{
			Renderer::printf("Error: Bound range exceeded.\n");
			break;
		}
		case 6:
		{
			Renderer::printf("Error: Invalid opcode.\n");
			break;
		}
		case 7:
		{
			Renderer::printf("Error: Device not available.\n");
			break;
		}
		case 8:
		{
			Renderer::printf("Error: Double fault.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 9:
		{
			Renderer::printf("Error: Coprocessor segment overrun.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 10:
		case 11:
		case 12:
		case 13:
		{
			const char* table;
			switch (errorCode & 0b11)
			{
				case 0:
					table = "GDT";
					break;
				case 1:
					table = "IDT";
					break;
				case 2:
					table = "LDT";
					break;
				default:
					table = "Unknown";
					break;
			}

			Renderer::printf("TSS Selector: Index = %lu, Table = %s\n", errorCode >> 3, table);
			break;
		}
		case 14:
		{
			uint64_t faultAddr;
			asm volatile("mov %%cr2, %0" : "=r"(faultAddr));
			Renderer::printf("Address: 0x%lx\nError Code: 0x%lx (%lu)\nPage Fault Details:\n", faultAddr, errorCode,
			                 errorCode);

			if (!(errorCode & 1)) Renderer::printf("- Page not present\n");
			if (errorCode & 2) Renderer::printf("- Write operation\n");
			if (errorCode & 4) Renderer::printf("- User mode access\n");
			if (errorCode & 8) Renderer::printf("- Reserved bit violation\n");
			if (errorCode & 16) Renderer::printf("- Instruction fetch\n");
			if (errorCode & (1 << 5)) Renderer::printf("- Protection-key violation\n");
			if (errorCode & (1 << 6)) Renderer::printf("- Shadow stack access violation\n");
			if (errorCode & (1 << 15)) Renderer::printf("- SGX access violation\n");

			break;
		}
		case 15:
		{
			Renderer::printf("Error: General protection fault.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 16:
		{
			Renderer::printf("Error: Reserved exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 17:
		{
			Renderer::printf("Error: Floating-point error.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 18:
		{
			Renderer::printf("Error: Alignment check.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 19:
		{
			Renderer::printf("Error: Machine check.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 20:
		{
			Renderer::printf("Error: SIMD floating-point exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 21:
		{
			Renderer::printf("Error: Virtualization exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 22:
		{
			Renderer::printf("Error: Control protection exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		case 23:
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
		{
			Renderer::printf("Error: Reserved exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
		default:
		{
			Renderer::printf("Error: Unknown exception.\n");
			if (errorCode) Renderer::printf("Error Code: 0x%lx (%lu)\n", errorCode, errorCode);

			break;
		}
	}

	Renderer::printf("RIP: 0x%lx\nCS: 0x%lx\nRSP: 0x%lx\nSS: 0x%lx\nRFLAGS: 0x%lx\n", frame->rip, frame->cs, frame->rsp,
	                 frame->ss, frame->rflags);
	Renderer::setSerialPrint(false);
	Renderer::printf("System Halted.\x1b[0m\n");

	while (true) asm volatile("hlt");
}

#define ISR_NOERR(n) __attribute__((interrupt)) void isr##n(InterruptFrame* frame) { showException(frame, n, 0); }
#define ISR_ERR(n) __attribute__((interrupt)) void isr##n(InterruptFrame* frame, uint64_t error) { showException(frame, n, error); }

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
