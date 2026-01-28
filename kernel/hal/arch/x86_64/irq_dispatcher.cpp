#include <stdint.h>
#include "io.h"
#include "../../video/early_term.h"

// Forward declaration of specific drivers
namespace PIT { void OnInterrupt(); }

#include "../../input/input_device.h"


struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

#include "../../../process/scheduler.h"

extern "C" uint64_t* GenericIRQHandler(Registers* regs) {
    uint64_t irq = regs->interrupt_number - 32;

    if (irq >= 8) {
        IO::outb(0xA0, 0x20); // Slave EOI
    }
    IO::outb(0x20, 0x20); // Master EOI

    switch (irq) {
        case 0: // Timer (IRQ0)
            PIT::OnInterrupt();
            return Scheduler::Schedule((uint64_t*)regs);
        case 1: // Keyboard (IRQ1)
            InputManager::DispatchInterrupt(1);
            break;
        case 12: // Mouse (IRQ12)
            InputManager::DispatchInterrupt(12);
            break;
        default:
            break;
    }
    
    return (uint64_t*)regs;
}
