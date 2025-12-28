#include <stdint.h>
#include "../../video/early_term.h"
#include "../../arch/x86_64/idt.h"

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
    
    // Clear screen to red (panic)
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
    
    EarlyTerm::Print("\nSystem Halted.");
    
    while(1) {
        __asm__("hlt");
    }
}
