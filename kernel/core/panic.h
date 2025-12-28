#pragma once

// Enhanced Kernel Panic System
// Provides crash logging, serial output, and VFS persistence

#include <stdint.h>

// Panic information structure
struct PanicInfo {
    const char* message;
    const char* file;
    int line;
    uint64_t rip;       // Instruction pointer
    uint64_t rsp;       // Stack pointer
    uint64_t error_code;
};

namespace KernelPanic {
    // Initialize panic system (enable serial if available)
    void Init();
    
    // Main panic function - halts the system
    void Panic(const char* message, const char* file, int line);
    
    // Panic with full register context
    void PanicWithContext(const char* message, void* cpu_context);
    
    // Output methods
    void WriteToScreen(const char* msg);
    void WriteToSerial(const char* msg);
    void WriteToVFS(const char* path);
    
    // Dump registers (architecture-specific)
    void DumpRegisters(void* context);
    
    // Save panic log to persistent storage
    bool SavePanicLog();
}

// Panic macros
#define KERNEL_PANIC(msg) \
    KernelPanic::Panic(msg, __FILE__, __LINE__)

#define KERNEL_ASSERT(cond, msg) \
    do { if (!(cond)) KERNEL_PANIC(msg); } while(0)
