#include "../../platform.h"
#include "../../video/early_term.h"
#include "../../serial/uart.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "syscall.h"
#include "tss.h"
#include "io.h"

namespace HAL {

    // Use the robust 64KB stack defined in kernel/core/entry.asm
    extern "C" uint64_t kernel_stack_top;

    void Platform::Init() {
        // Disable interrupts during initialization
        DisableInterrupts();

        // 0. Enable x87 FPU and SSE/SSE2 (FXSAVE/FXRSTOR + XMM0-XMM15)
        uint64_t cr0, cr4;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ULL << 2); // Clear CR0.EM (Emulation)
        cr0 |= (1ULL << 1);  // Set CR0.MP (Monitor Coprocessor)
        __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 9) | (1ULL << 10); // Set CR4.OSFXSR | CR4.OSXMMEXCPT
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
        __asm__ volatile("fninit");
        uint32_t mxcsr = 0x1F80; // Default MXCSR: all SIMD FP exceptions masked
        __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));

        // 1. Initialize Serial Port for Debugging
        UART::Init();
        EarlyTerm::Print("[HAL] Serial Initialized.\n");

        // 2. Initialize GDT (Global Descriptor Table)
        GDT::Init();
        EarlyTerm::Print("[HAL] GDT Initialized.\n");
        
        // 2b. Initialize TSS (Task State Segment) for Ring 3 support
        // Point TSS RSP0 to the safe 64KB Kernel Stack
        TSS::Init((uint64_t)&kernel_stack_top);
        GDT::LoadTSS(TSS::GetTSS());
        EarlyTerm::Print("[HAL] TSS Initialized.\n");

        // 3. Initialize IDT (Interrupt Descriptor Table)
        IDT::Init();
        EarlyTerm::Print("[HAL] IDT Initialized.\n");

        // 4. Initialize PIC (Programmable Interrupt Controller)
        PIC::Remap();
        EarlyTerm::Print("[HAL] PIC Initialized.\n");

        // 5. Initialize Syscall MSRs
        Syscall::Init();
        EarlyTerm::Print("[HAL] Syscall Interface Initialized.\n");

        // Enable Interrupts
        EnableInterrupts();
        EarlyTerm::Print("[HAL] Platform Initialization Complete.\n");
    }

    void Platform::EnableInterrupts() {
        __asm__ volatile("sti");
    }

    void Platform::DisableInterrupts() {
        __asm__ volatile("cli");
    }

    bool Platform::AreInterruptsEnabled() {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
        return (rflags & 0x200) != 0; // IF bit is bit 9
    }

    void Platform::Halt() {
        __asm__ volatile("hlt");
    }

    void Platform::Reboot() {
        uint8_t good = 0x02;
        while (good & 0x02)
            good = IO::inb(0x64);
        IO::outb(0x64, 0xFE);
        Halt();
    }
}
