#pragma once

// Morphic OS Kernel Debug Facilities
// Provides serial trace points and safe panic for bootloop diagnosis

#include "../hal/serial/uart.h"

// =============================================================================
// TRACE MACROS - Use these to trace execution through critical code paths
// Output goes to serial console (COM1) for capture even during crashes
// =============================================================================

// Simple checkpoint - prints a message to serial
#define TRACE_CHECKPOINT(msg) do { \
    UART::Write("[TRACE] "); \
    UART::Write(msg); \
    UART::Write("\n"); \
} while(0)

// Trace with hex value - useful for addresses and sizes
#define TRACE_HEX(label, val) do { \
    UART::Write("[TRACE] "); \
    UART::Write(label); \
    UART::Write(": "); \
    UART::WriteHex((uint64_t)(val)); \
    UART::Write("\n"); \
} while(0)

// Trace with decimal value - useful for counts and indices
#define TRACE_DEC(label, val) do { \
    UART::Write("[TRACE] "); \
    UART::Write(label); \
    UART::Write(": "); \
    UART::WriteDec((int64_t)(val)); \
    UART::Write("\n"); \
} while(0)

// =============================================================================
// KERNEL PANIC SAFE - Halts system with serial output instead of reboot
// Use when you want to capture the error state before CPU triple faults
// =============================================================================

#define KERNEL_PANIC_SAFE(msg) do { \
    __asm__ volatile("cli"); \
    UART::Write("\n"); \
    UART::Write("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); \
    UART::Write("!!!            KERNEL PANIC (SAFE)             !!!\n"); \
    UART::Write("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); \
    UART::Write("Reason: "); \
    UART::Write(msg); \
    UART::Write("\n"); \
    UART::Write("System halted. Check serial output above.\n"); \
    while(1) { __asm__ volatile("hlt"); } \
} while(0)

// Extended panic with register dump (inline assembly to capture current state)
#define KERNEL_PANIC_DUMP(msg) do { \
    __asm__ volatile("cli"); \
    uint64_t _rip, _rsp, _rbp, _rax, _rbx, _rcx, _rdx; \
    __asm__ volatile("lea (%%rip), %0" : "=r"(_rip)); \
    __asm__ volatile("mov %%rsp, %0" : "=r"(_rsp)); \
    __asm__ volatile("mov %%rbp, %0" : "=r"(_rbp)); \
    __asm__ volatile("mov %%rax, %0" : "=r"(_rax)); \
    __asm__ volatile("mov %%rbx, %0" : "=r"(_rbx)); \
    __asm__ volatile("mov %%rcx, %0" : "=r"(_rcx)); \
    __asm__ volatile("mov %%rdx, %0" : "=r"(_rdx)); \
    UART::Write("\n!!! KERNEL PANIC DUMP !!!\n"); \
    UART::Write("Reason: "); UART::Write(msg); UART::Write("\n"); \
    UART::Write("RIP: "); UART::WriteHex(_rip); UART::Write("\n"); \
    UART::Write("RSP: "); UART::WriteHex(_rsp); UART::Write("\n"); \
    UART::Write("RBP: "); UART::WriteHex(_rbp); UART::Write("\n"); \
    UART::Write("RAX: "); UART::WriteHex(_rax); \
    UART::Write(" RBX: "); UART::WriteHex(_rbx); UART::Write("\n"); \
    UART::Write("RCX: "); UART::WriteHex(_rcx); \
    UART::Write(" RDX: "); UART::WriteHex(_rdx); UART::Write("\n"); \
    UART::Write("System halted.\n"); \
    while(1) { __asm__ volatile("hlt"); } \
} while(0)

// =============================================================================
// ASSERT MACRO - Conditional panic for invariant checking
// =============================================================================

#define KERNEL_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        KERNEL_PANIC_SAFE("ASSERT FAILED: " msg); \
    } \
} while(0)
