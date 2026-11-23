#include <arch/x86_64/isr.h>
#include <arch/x86_64/smp.h>
#include <core/panic.h>
#include <drivers/serial.h>
#include <memory/paging.h>

uint32_t panicClaimed = 0;
[[noreturn]] void halt() { while (true) asm volatile ("cli; hlt"); }

void beginPanic()
{
    Interrupt::disableInterrupts();
    if (__atomic_exchange_n(&panicClaimed, 1u, __ATOMIC_ACQ_REL)) halt();

    SMP::stopOthers();
    Serial::emergencyPrintf("\n================ KERNEL PANIC ================\n");
}

[[noreturn]] void Panic::panic(const char* fmt, ...)
{
    beginPanic();
    va_list args;
    va_start(args, fmt);
    Serial::emergencyVprintf(fmt, args);
    va_end(args);
    Serial::emergencyPrintf("\n");

    printStackTrace();
    halt();
}

[[noreturn]] void Panic::panicFrame(const Interrupt::Frame* frame, const char* fmt, ...)
{
    beginPanic();
    if (frame)
        Serial::emergencyPrintf("RIP: 0x%lx RSP: 0x%lx CS: 0x%lx SS: 0x%lx RFLAGS: 0x%lx\n",
                                frame->rip, frame->rsp, frame->cs, frame->ss, frame->rflags);

    va_list args;
    va_start(args, fmt);
    Serial::emergencyVprintf(fmt, args);
    va_end(args);
    Serial::emergencyPrintf("\n");

    printStackTrace();
    halt();
}

void Panic::printStackTrace(uint64_t rbp, const int maxFrames)
{
    if (!rbp) asm volatile ("mov %%rbp, %0" : "=r"(rbp));
    for (int i = 0; i < maxFrames && i < 64; ++i)
    {
        if (!rbp || (rbp & 7) || rbp > UINT64_MAX - 16 || !Paging::isMapped(rbp) || !Paging::isMapped(rbp + 15)) break;

        const auto* frame = reinterpret_cast<const uint64_t*>(rbp);
        Serial::emergencyPrintf("#%d: 0x%lx\n", i, frame[1]);
        if (frame[0] <= rbp) break;
        rbp = frame[0];
    }
}
