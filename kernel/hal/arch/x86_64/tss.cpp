#include "tss.h"
#include "../../video/early_term.h"
#include "../../../utils/std.h"

namespace TSS {
    static TSS64 tss;
    
    void Init(uint64_t kernel_stack) {
        kmemset(&tss, 0, sizeof(TSS64));
        
        // Set kernel stack for Ring 0 (used when interrupting from Ring 3)
        tss.rsp0 = kernel_stack;
        
        // I/O Permission Bitmap offset (set beyond TSS limit = no IOPB)
        tss.iopb = sizeof(TSS64);
        
        EarlyTerm::Print("[TSS] Initialized. RSP0: 0x");
        EarlyTerm::PrintHex(kernel_stack);
        EarlyTerm::Print("\n");
    }
    
    TSS64* GetTSS() {
        return &tss;
    }
    
    void SetKernelStack(uint64_t stack) {
        tss.rsp0 = stack;
    }
}
