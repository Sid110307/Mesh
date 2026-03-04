#include <arch/x86_64/irq.h>
#include <core/utils.h>

bool IRQ::interruptsEnabled()
{
    uint64_t rflags;
    asm volatile("pushfq\npopq %0" : "=r"(rflags) :: "memory");

    return (rflags & (1ULL << 9)) != 0;
}

void IRQ::enableInterrupts() { asm volatile("sti" ::: "memory"); }
void IRQ::disableInterrupts() { asm volatile("cli" ::: "memory"); }
