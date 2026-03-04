#include <arch/x86_64/irq.h>
#include <arch/x86_64/smp.h>
#include <core/panic.h>
#include <drivers/renderer.h>

uint64_t readRbp()
{
    uint64_t rbp;
    asm volatile("mov %%rbp, %0" : "=r"(rbp));

    return rbp;
}

void printHeader()
{
    Renderer::setSerialPrint(true);
    Renderer::printf("\n\x1b[31m==================== KERNEL PANIC ====================\x1b[0m\n");
    Renderer::printf("\x1b[90mCPU ID:\x1b[0m %u | \x1b[90mLAPIC ID:\x1b[0m %u\n", SMP::getLapicID(), SMP::getLapicID());
}

void Panic::panic(const char* fmt, ...)
{
    IRQ::disableInterrupts();
    printHeader();

    va_list args;
    va_start(args, fmt);
    vformat(fmt, args, [](const char c) { Renderer::printChar(c, WHITE, BLACK); },
            [](const char* s) { Renderer::print(s, WHITE, BLACK); },
            [](const uint64_t h) { Renderer::printHex(h, WHITE, BLACK); },
            [](const uint64_t d) { Renderer::printDec(d, WHITE, BLACK); });
    va_end(args);

    Renderer::printf("\n\x1b[31m======================================================\x1b[0m\n");
    printStackTrace();

    while (true) asm volatile("hlt");
}

void Panic::panicFrame(const InterruptFrame* frame, const char* fmt, ...)
{
    IRQ::disableInterrupts();
    printHeader();

    if (frame)
    {
        Renderer::printf("\x1b[90mRIP:\x1b[0m 0x%lx | ", frame->rip);
        Renderer::printf("\x1b[90mRSP:\x1b[0m 0x%lx\n", frame->rsp);
        Renderer::printf("\x1b[90mRFLAGS:\x1b[0m 0x%lx | ", frame->rflags);
        Renderer::printf("\x1b[90mCS:\x1b[0m 0x%lx | ", frame->cs);
        Renderer::printf("\x1b[90mSS:\x1b[0m 0x%lx\n", frame->ss);
    }

    if (fmt && *fmt)
    {
        va_list args;
        va_start(args, fmt);
        vformat(fmt, args, [](const char c) { Renderer::printChar(c, WHITE, BLACK); },
                [](const char* s) { Renderer::print(s, WHITE, BLACK); },
                [](const uint64_t h) { Renderer::printHex(h, WHITE, BLACK); },
                [](const uint64_t d) { Renderer::printDec(d, WHITE, BLACK); });
        va_end(args);
    }

    Renderer::printf("\n\x1b[31m======================================================\x1b[0m\n");
    printStackTrace();

    while (true) asm volatile("hlt");
}

void Panic::printStackTrace(uint64_t basePointer, const int maxFrames)
{
    if (basePointer == 0) basePointer = readRbp();

    Renderer::printf("\n\x1b[90mStack Trace (RBP: 0x%lx):\x1b[0m\n", basePointer);
    uint64_t rbp = basePointer;

    for (int i = 0; i < maxFrames; ++i)
    {
        if (rbp == 0 || (rbp & 0x7) != 0)
        {
            Renderer::printf("\x1b[90m[End of stack trace]\x1b[0m\n");
            break;
        }

        const auto* frame = reinterpret_cast<const uint64_t*>(rbp);
        Renderer::printf("\x1b[90m#%d: 0x%lx\x1b[0m\n", i, frame[1]);

        if (frame[0] <= rbp)
        {
            Renderer::printf("\x1b[90m[Invalid frame pointer: 0x%lx]\x1b[0m\n", frame[0]);
            break;
        }
        rbp = frame[0];
    }
}
