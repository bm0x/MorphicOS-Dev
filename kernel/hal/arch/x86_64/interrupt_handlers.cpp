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
    UART::Write("================================================================================\n");
    UART::Write("                            MORPHIC OS KERNEL PANIC                            \n");
    UART::Write("================================================================================\n\n");
    
    UART::Write("[EXCEPTION] #");
    UART::WriteDec(regs->interrupt_number);
    UART::Write(" - ");
    if (regs->interrupt_number < 32) {
        UART::Write(exception_messages[regs->interrupt_number]);
    } else {
        UART::Write("Unknown/IRQ");
    }
    UART::Write("\n");
    UART::Write("[ERROR CODE] 0x");
    UART::WriteHex(regs->error_code);
    UART::Write("\n\n");
    
    UART::Write("--- CPU State ---\n");
    UART::Write("RIP:    0x"); UART::WriteHex(regs->rip); UART::Write("\n");
    UART::Write("RSP:    0x"); UART::WriteHex(regs->rsp); UART::Write("\n");
    UART::Write("RBP:    0x"); UART::WriteHex(regs->rbp); UART::Write("\n");
    UART::Write("RFLAGS: 0x"); UART::WriteHex(regs->rflags); UART::Write("\n");
    UART::Write("CS:     0x"); UART::WriteHex(regs->cs); UART::Write("\n");
    UART::Write("SS:     0x"); UART::WriteHex(regs->ss); UART::Write("\n\n");
    
    UART::Write("--- Registers ---\n");
    UART::Write("RAX: 0x"); UART::WriteHex(regs->rax);
    UART::Write("  RBX: 0x"); UART::WriteHex(regs->rbx);
    UART::Write("  RCX: 0x"); UART::WriteHex(regs->rcx); UART::Write("\n");
    UART::Write("RDX: 0x"); UART::WriteHex(regs->rdx);
    UART::Write("  RSI: 0x"); UART::WriteHex(regs->rsi);
    UART::Write("  RDI: 0x"); UART::WriteHex(regs->rdi); UART::Write("\n");
    UART::Write("R8:  0x"); UART::WriteHex(regs->r8);
    UART::Write("  R9:  0x"); UART::WriteHex(regs->r9);
    UART::Write("  R10: 0x"); UART::WriteHex(regs->r10); UART::Write("\n");
    UART::Write("R11: 0x"); UART::WriteHex(regs->r11);
    UART::Write("  R12: 0x"); UART::WriteHex(regs->r12);
    UART::Write("  R13: 0x"); UART::WriteHex(regs->r13); UART::Write("\n");
    UART::Write("R14: 0x"); UART::WriteHex(regs->r14);
    UART::Write("  R15: 0x"); UART::WriteHex(regs->r15); UART::Write("\n\n");
    
    // Page Fault specific info
    if (regs->interrupt_number == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        UART::Write("--- Page Fault Details ---\n");
        UART::Write("Faulting Address (CR2): 0x");
        UART::WriteHex(cr2);
        UART::Write("\nAccess Type: ");
        if (regs->error_code & 0x1) UART::Write("Protection Violation");
        else UART::Write("Page Not Present");
        UART::Write(" | ");
        if (regs->error_code & 0x2) UART::Write("Write Access");
        else UART::Write("Read Access");
        UART::Write(" | ");
        if (regs->error_code & 0x4) UART::Write("User Mode");
        else UART::Write("Kernel Mode");
        UART::Write("\n\n");
    }
    
    UART::Write("================================================================================\n");
    UART::Write("System Halted. Review logs above for debugging information.\n");
    
    // ========================================
    // SCREEN OUTPUT - Professional Dark Theme
    // ========================================
    
    // Force-enable EarlyTerm if it was disabled by Desktop
    EarlyTerm::ForceEnable();
    
    // Dark maroon background with white text for professional look
    EarlyTerm::SetColor(0xFFFFFFFF, 0xFF400000);
    EarlyTerm::Clear();
    
    // Title Section
    EarlyTerm::Print("\n\n");
    EarlyTerm::Print("    ============================================================\n");
    EarlyTerm::Print("                    MORPHIC OS - KERNEL PANIC                    \n");
    EarlyTerm::Print("    ============================================================\n\n");
    
    // Exception Info
    EarlyTerm::Print("    EXCEPTION : #");
    EarlyTerm::PrintDec(regs->interrupt_number);
    EarlyTerm::Print(" - ");
    if (regs->interrupt_number < 32) {
        EarlyTerm::Print(exception_messages[regs->interrupt_number]);
    } else {
        EarlyTerm::Print("Unknown Exception");
    }
    EarlyTerm::Print("\n");
    
    EarlyTerm::Print("    ERROR CODE: 0x");
    EarlyTerm::PrintHex(regs->error_code);
    EarlyTerm::Print("\n\n");
    
    // Segment: CPU State
    EarlyTerm::Print("    --- Execution Context ---\n");
    EarlyTerm::Print("    Instruction Pointer : 0x");
    EarlyTerm::PrintHex(regs->rip);
    EarlyTerm::Print("\n");
    EarlyTerm::Print("    Stack Pointer       : 0x");
    EarlyTerm::PrintHex(regs->rsp);
    EarlyTerm::Print("\n");
    EarlyTerm::Print("    Code Segment        : 0x");
    EarlyTerm::PrintHex(regs->cs);
    EarlyTerm::Print("\n");
    EarlyTerm::Print("    Flags               : 0x");
    EarlyTerm::PrintHex(regs->rflags);
    EarlyTerm::Print("\n\n");
    
    // Page Fault Details
    if (regs->interrupt_number == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        EarlyTerm::Print("    --- Page Fault Details ---\n");
        EarlyTerm::Print("    Fault Address : 0x");
        EarlyTerm::PrintHex(cr2);
        EarlyTerm::Print("\n");
        EarlyTerm::Print("    Access        : ");
        if (regs->error_code & 0x2) EarlyTerm::Print("WRITE");
        else EarlyTerm::Print("READ");
        EarlyTerm::Print(" in ");
        if (regs->error_code & 0x4) EarlyTerm::Print("USER");
        else EarlyTerm::Print("KERNEL");
        EarlyTerm::Print(" mode\n\n");
    }
    
    // Footer
    EarlyTerm::Print("    ============================================================\n");
    EarlyTerm::Print("      System halted. Check serial console for full debug info.\n");
    EarlyTerm::Print("    ============================================================\n");
    
    // Infinite halt loop
    while(1) {
        __asm__("hlt");
    }
}



