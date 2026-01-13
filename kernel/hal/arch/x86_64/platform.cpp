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

    // Kernel stack for TSS (4KB aligned) - Moved from kernel_main
    static uint8_t kernel_tss_stack[4096] __attribute__((aligned(16)));

    void Platform::Init() {
        // Disable interrupts during initialization
        DisableInterrupts();

        // 1. Initialize Serial Port for Debugging
        UART::Init();
        EarlyTerm::Print("[HAL] Serial Initialized.\n");

        // 2. Initialize GDT (Global Descriptor Table)
        GDT::Init();
        EarlyTerm::Print("[HAL] GDT Initialized.\n");
        
        // 2b. Initialize TSS (Task State Segment) for Ring 3 support
        TSS::Init((uint64_t)&kernel_tss_stack[4096]);
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
