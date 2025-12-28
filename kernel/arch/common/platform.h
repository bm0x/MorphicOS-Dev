#pragma once

// Platform Abstraction Layer
// Architecture-agnostic interface for CPU primitives

#include <stdint.h>

namespace Platform {
    // Initialize platform-specific hardware
    void Init();
    
    // CPU control
    void Halt();                    // Halt CPU (low power wait)
    void DisableInterrupts();       // CLI equivalent
    void EnableInterrupts();        // STI equivalent
    
    // Memory barriers (for DMA and multicore)
    void MemoryBarrier();           // Full memory fence
    void ReadBarrier();             // Ensure reads complete
    void WriteBarrier();            // Ensure writes complete
    
    // Interrupt controller
    void MaskIRQ(uint8_t irq);
    void UnmaskIRQ(uint8_t irq);
    void SendEOI(uint8_t irq);
    
    // CPU identification
    const char* GetArchName();
    uint32_t GetCPUFrequencyMHz();
}
