#include <stdint.h>
#include "../../video/early_term.h"
#include "../../arch/x86_64/idt.h"
#include "../../serial/uart.h"
#include "../../../process/scheduler.h"

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

extern "C" uint64_t* GenericExceptionHandler(Registers* regs) {
    // Disable interrupts during exception handling
    __asm__ volatile("cli");

    // Isolate Ring 3 Application Faults: kill ONLY the offending process instead of halting the OS!
    if ((regs->cs & 0x3) == 3) {
        Task* cur = Scheduler::GetCurrentTask();
        if (cur && cur->id != 0) {
            UART::Write("\n[USER FAULT] Terminating Ring 3 Task PID=");
            UART::WriteDec(cur->id);
            UART::Write(" Exception=#");
            UART::WriteDec(regs->interrupt_number);
            if (regs->interrupt_number < 32) {
                UART::Write(" (");
                UART::Write(exception_messages[regs->interrupt_number]);
                UART::Write(")");
            }
            UART::Write(" RIP=0x");
            UART::WriteHex(regs->rip);
            UART::Write(" RSP=0x");
            UART::WriteHex(regs->rsp);
            if (regs->interrupt_number == 14) {
                uint64_t cr2;
                __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
                UART::Write(" CR2=0x");
                UART::WriteHex(cr2);
            }
            UART::Write("\n");

            cur->exit_code = -128 - (int)regs->interrupt_number;
            cur->state = TaskState::DEAD;
            return Scheduler::Schedule((uint64_t*)regs);
        }
    }
    
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
    
    if (regs->interrupt_number == 13 || regs->interrupt_number == 12) {
        // IRETQ-failure forensics: dump the 5-qword return frame and the live GDT.
        UART::Write("--- IRET frame @RSP (rip,cs,rflags,rsp,ss) ---\n");
        uint64_t* fr = (uint64_t*)regs->rsp;
        for (int i = 0; i < 5; i++) { UART::Write("  "); UART::WriteHex(fr[i]); UART::Write("\n"); }
        uint64_t gdtr[2] = {0, 0};
        __asm__ volatile("sgdt %0" : "=m"(*(char(*)[10])gdtr));
        UART::Write("--- GDT ---\n");
        uint64_t base = ((uint64_t)((uint8_t*)gdtr)[2]) | ((uint64_t)((uint8_t*)gdtr)[3] << 8) | ((uint64_t)((uint8_t*)gdtr)[4] << 16) | ((uint64_t)((uint8_t*)gdtr)[5] << 24) | ((uint64_t)((uint8_t*)gdtr)[6] << 32) | ((uint64_t)((uint8_t*)gdtr)[7] << 40) | ((uint64_t)((uint8_t*)gdtr)[8] << 48) | ((uint64_t)((uint8_t*)gdtr)[9] << 56);
        for (int i = 0; i < 7; i++) { UART::Write("  ["); UART::WriteDec(i); UART::Write("] "); UART::WriteHex(*(uint64_t*)(base + i * 8)); UART::Write("\n"); }
        uint64_t cr0, cr3, cr4, efer;
        __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2" : "=r"(cr0), "=r"(cr3), "=r"(cr4));
        uint32_t lo, hi; __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080)); efer = ((uint64_t)hi << 32) | lo;
        UART::Write("CR0="); UART::WriteHex(cr0); UART::Write(" CR3="); UART::WriteHex(cr3); UART::Write(" CR4="); UART::WriteHex(cr4); UART::Write(" EFER="); UART::WriteHex(efer); UART::Write("\n\n");
    }
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
    return (uint64_t*)regs;
}



