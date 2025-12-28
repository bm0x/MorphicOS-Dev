#pragma once

// Exception Handler Abstraction
// Architecture-independent exception and interrupt interface

#include <stdint.h>

// Exception frame (architecture-agnostic view of saved context)
struct ExceptionFrame {
    uint64_t gpr[16];       // General purpose registers
    uint64_t pc;            // Program counter (RIP/ELR)
    uint64_t sp;            // Stack pointer
    uint64_t flags;         // CPU flags (RFLAGS/SPSR)
    uint32_t vector;        // Exception/interrupt number
    uint32_t error_code;    // Error code (if applicable)
};

// Exception types (common across architectures)
enum class ExceptionType {
    DIVIDE_ERROR = 0,
    DEBUG,
    NMI,
    BREAKPOINT,
    OVERFLOW,
    BOUND_RANGE,
    INVALID_OPCODE,
    DEVICE_NOT_AVAILABLE,
    DOUBLE_FAULT,
    INVALID_TSS,
    SEGMENT_NOT_PRESENT,
    STACK_FAULT,
    GENERAL_PROTECTION,
    PAGE_FAULT,
    FPU_ERROR,
    ALIGNMENT_CHECK,
    MACHINE_CHECK,
    SIMD_ERROR,
    
    // Hardware interrupts start here
    IRQ_BASE = 32,
    IRQ_TIMER = 32,
    IRQ_KEYBOARD = 33,
    IRQ_CASCADE = 34,
    IRQ_COM2 = 35,
    IRQ_COM1 = 36,
    IRQ_LPT2 = 37,
    IRQ_FLOPPY = 38,
    IRQ_LPT1 = 39,
    IRQ_RTC = 40,
    IRQ_MOUSE = 44,
    
    // Syscall
    SYSCALL = 128
};

// Exception handler function type
typedef void (*ExceptionHandler)(ExceptionFrame* frame);

namespace Exceptions {
    // Initialize exception handling (IDT for x86, vectors for ARM)
    void Init();
    
    // Register a handler for a specific exception/interrupt
    void RegisterHandler(uint32_t vector, ExceptionHandler handler);
    
    // Unregister a handler
    void UnregisterHandler(uint32_t vector);
    
    // Get current handler for a vector
    ExceptionHandler GetHandler(uint32_t vector);
    
    // Enable/disable interrupts
    void EnableInterrupts();
    void DisableInterrupts();
    
    // Check if interrupts are enabled
    bool InterruptsEnabled();
    
    // Send End-Of-Interrupt signal
    void SendEOI(uint32_t vector);
    
    // Default handlers
    void DefaultExceptionHandler(ExceptionFrame* frame);
    void DefaultIRQHandler(ExceptionFrame* frame);
}

// For use in ISR context
#define SAVE_CONTEXT()    /* Architecture-specific context save */
#define RESTORE_CONTEXT() /* Architecture-specific context restore */
