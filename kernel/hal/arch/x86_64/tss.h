#pragma once

#include <stdint.h>

// TSS64 - Task State Segment for x86_64
// Required for Ring 3 -> Ring 0 transitions
struct __attribute__((packed)) TSS64 {
    uint32_t reserved0;
    uint64_t rsp0;      // Stack pointer for Ring 0 (interrupt from Ring 3)
    uint64_t rsp1;      // Stack pointer for Ring 1 (unused)
    uint64_t rsp2;      // Stack pointer for Ring 2 (unused)
    uint64_t reserved1;
    uint64_t ist[7];    // Interrupt Stack Table entries
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;      // I/O Permission Bitmap offset
};

namespace TSS {
    // Initialize TSS with kernel stack
    void Init(uint64_t kernel_stack);
    
    // Get TSS pointer (for GDT setup)
    TSS64* GetTSS();
    
    // Update RSP0 (for task switching)
    void SetKernelStack(uint64_t stack);
}
