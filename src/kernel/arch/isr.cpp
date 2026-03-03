#include <kernel/arch/isr.h>
#include <drivers/video/renderer.h>
#include <drivers/io/keyboard/keyboard.h>
#include <drivers/io/serial/serial.h>
#include <memory/lapic.h>

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
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 9:
            {
                Renderer::printf("Error: Coprocessor segment overrun.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 10:
            {
                Renderer::printf("Error: Invalid TSS.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 11:
            {
                Renderer::printf("Error: Segment not present.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 12:
            {
                Renderer::printf("Error: Stack segment fault.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
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

                Renderer::printf("Error: General protection fault.\nIndex = %lu, Table = %s\n", errorCode >> 3, table);
                break;
            }
        case 14:
            {
                uint64_t faultAddr;
                asm volatile ("mov %%cr2, %0" : "=r"(faultAddr));
                Renderer::printf("Error: Page fault.\nAddress: 0x%lx\nError Code: 0x%x (%lu)\nDetails:\n", faultAddr,
                                 errorCode, errorCode);

                if (!(errorCode & 1)) Renderer::printf("- Page not present\n");
                if (errorCode & 2) Renderer::printf("- Write operation\n");
                if (errorCode & 4) Renderer::printf("- User mode access\n");
                if (errorCode & 8) Renderer::printf("- Reserved bit violation\n");
                if (errorCode & 16) Renderer::printf("- Instruction fetch\n");
                if (errorCode & 1 << 5) Renderer::printf("- Protection-key violation\n");
                if (errorCode & 1 << 6) Renderer::printf("- Shadow stack access violation\n");
                if (errorCode & 1 << 15) Renderer::printf("- SGX access violation\n");

                break;
            }
        case 16:
            {
                Renderer::printf("Error: Floating-point error.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 17:
            {
                Renderer::printf("Error: Alignment check.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 18:
            {
                Renderer::printf("Error: Machine check.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 19:
            {
                Renderer::printf("Error: SIMD floating-point exception.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 20:
            {
                Renderer::printf("Error: Virtualization exception.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 21:
            {
                Renderer::printf("Error: Control protection exception.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        case 15:
        case 22:
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
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
        default:
            {
                Renderer::printf("Error: Unknown exception.\n");
                if (errorCode) Renderer::printf("Error Code: 0x%x (%lu)\n", errorCode, errorCode);

                break;
            }
    }

    Renderer::printf("RIP: 0x%lx\nCS: 0x%lx\nRFLAGS: 0x%lx\nSS: 0x%lx\nRSP: 0x%lx\n", frame->rip, frame->cs,
                     frame->rflags, frame->ss, frame->rsp);
    Renderer::setSerialPrint(false);
    Renderer::printf("\x1b[0mSystem Halted.\n");

    while (true) asm volatile ("hlt");
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
}
