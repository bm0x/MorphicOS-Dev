#include <stdint.h>
#include "../../video/early_term.h"
#include "../../arch/x86_64/idt.h"
#include "../../serial/uart.h"

struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

const char* exception_messages[] = {
    "Division By Zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present", "Stack Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Float", "Alignment Check", "Machine Check", "SIMD Float", "Virtualization", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Security", "Reserved"
};

extern "C" void GenericExceptionHandler(Registers* regs) {
    // Disable interrupts during panic
    __asm__ volatile("cli");
    
    // ========================================
    // SERIAL OUTPUT FIRST - Always works even if GPU/memory corrupted
    // ========================================
    UART::Write("\n");
    UART::Write("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    UART::Write("!!!            KERNEL PANIC                    !!!\n");
    UART::Write("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    
    UART::Write("Exception: ");
    UART::WriteDec(regs->interrupt_number);
    UART::Write(" (");
    if (regs->interrupt_number < 32) {
        UART::Write(exception_messages[regs->interrupt_number]);
    } else {
        UART::Write("IRQ/Unknown");
    }
    UART::Write(")\n");
    
    UART::Write("Error Code: ");
    UART::WriteHex(regs->error_code);
    UART::Write("\n\n");
    
    UART::Write("RIP:    "); UART::WriteHex(regs->rip); UART::Write("\n");
    UART::Write("RSP:    "); UART::WriteHex(regs->rsp); UART::Write("\n");
    UART::Write("RBP:    "); UART::WriteHex(regs->rbp); UART::Write("\n");
    UART::Write("RFLAGS: "); UART::WriteHex(regs->rflags); UART::Write("\n");
    UART::Write("CS:     "); UART::WriteHex(regs->cs); UART::Write("\n");
    UART::Write("SS:     "); UART::WriteHex(regs->ss); UART::Write("\n\n");
    
    UART::Write("RAX: "); UART::WriteHex(regs->rax);
    UART::Write(" RBX: "); UART::WriteHex(regs->rbx);
    UART::Write(" RCX: "); UART::WriteHex(regs->rcx); UART::Write("\n");
    UART::Write("RDX: "); UART::WriteHex(regs->rdx);
    UART::Write(" RSI: "); UART::WriteHex(regs->rsi);
    UART::Write(" RDI: "); UART::WriteHex(regs->rdi); UART::Write("\n");
    UART::Write("R8:  "); UART::WriteHex(regs->r8);
    UART::Write(" R9:  "); UART::WriteHex(regs->r9);
    UART::Write(" R10: "); UART::WriteHex(regs->r10); UART::Write("\n");
    
    // Page Fault specific info
    if (regs->interrupt_number == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        UART::Write("\nPage Fault Address (CR2): ");
        UART::WriteHex(cr2);
        UART::Write("\n");
        UART::Write("Fault Type: ");
        if (regs->error_code & 0x1) UART::Write("Protection ");
        else UART::Write("NotPresent ");
        if (regs->error_code & 0x2) UART::Write("Write ");
        else UART::Write("Read ");
        if (regs->error_code & 0x4) UART::Write("User ");
        else UART::Write("Kernel ");
        UART::Write("\n");
    }
    
    UART::Write("\nSystem Halted. Check serial output.\n");
    
    // ========================================
    // SCREEN OUTPUT - May fail if memory is corrupted
    // ========================================
    EarlyTerm::SetColor(0xFFFFFFFF, 0xFFFF0000); // White on Red
    EarlyTerm::Clear();
    
    EarlyTerm::Print("\n--- MORPHIC KERNEL PANIC ---\n\n");
    
    EarlyTerm::Print("Exception: ");
    EarlyTerm::PrintDec(regs->interrupt_number);
    EarlyTerm::Print(" (");
    if (regs->interrupt_number < 32) {
        EarlyTerm::Print(exception_messages[regs->interrupt_number]);
    } else {
        EarlyTerm::Print("Unknown");
    }
    EarlyTerm::Print(")\n");
    
    EarlyTerm::Print("Error Code: ");
    EarlyTerm::PrintHex(regs->error_code);
    EarlyTerm::Print("\n\n");

    EarlyTerm::Print("RIP: "); EarlyTerm::PrintHex(regs->rip);
    EarlyTerm::Print("  RSP: "); EarlyTerm::PrintHex(regs->rsp);
    EarlyTerm::Print("  RFLAGS: "); EarlyTerm::PrintHex(regs->rflags);
    EarlyTerm::Print("\n");
    
    EarlyTerm::Print("RAX: "); EarlyTerm::PrintHex(regs->rax);
    EarlyTerm::Print("  RBX: "); EarlyTerm::PrintHex(regs->rbx);
    EarlyTerm::Print("  RCX: "); EarlyTerm::PrintHex(regs->rcx);
    EarlyTerm::Print("\n");
    
    if (regs->interrupt_number == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        EarlyTerm::Print("\nFault Address (CR2): ");
        EarlyTerm::PrintHex(cr2);
        EarlyTerm::Print("\n");
    }
    
    EarlyTerm::Print("\nSystem Halted. Check serial console for full dump.");
    
    while(1) {
        __asm__("hlt");
    }
}

