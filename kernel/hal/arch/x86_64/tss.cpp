#include "tss.h"
#include "../../video/early_term.h"
#include "../../../utils/std.h"

namespace TSS {
    static TSS64 tss;
    
    // Export pointer for assembly (syscall_entry.asm)
    extern "C" TSS64* kernel_tss_ptr = &tss;
    
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
        // Preserve current IF (interrupt flag) state, disable interrupts
        uint64_t rflags = 0;
        __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
        bool ints_enabled = (rflags & (1ULL << 9)) != 0;
        __asm__ volatile("cli");

        // Protect against concurrent updates on SMP systems using a simple spinlock.
        // We keep interrupts disabled on this CPU to avoid deadlocks with local IRQs.
        static volatile uint32_t tss_lock = 0;
        while (__atomic_test_and_set(&tss_lock, __ATOMIC_ACQUIRE)) {
            __asm__ volatile("pause");
        }

        // Perform the update (single 64-bit store)
        tss.rsp0 = stack;

        // Release spinlock
        __atomic_clear(&tss_lock, __ATOMIC_RELEASE);

        // Restore IF if it was enabled before
        if (ints_enabled) __asm__ volatile("sti");
    }
}
