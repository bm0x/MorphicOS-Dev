#include "syscall.h"
#include "../../video/early_term.h"
#include "../../video/compositor.h"
#include "../../video/graphics.h"
#include "../../audio/audio_device.h"
#include "../../serial/uart.h"
#include "../../../mm/heap.h"


// MSR read/write helpers
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Assembly entry point
extern "C" void syscall_entry();

namespace Syscall {
    void Init() {
        // STAR MSR Format:
        // [63:48] = SYSRET CS/SS base selector
        // [47:32] = SYSCALL CS/SS base selector
        //
        // For SYSCALL: CS = [47:32], SS = [47:32] + 8
        // For SYSRET (64-bit): CS = [63:48] + 16, SS = [63:48] + 8
        //
        // Our GDT layout:
        //   0x08 = Kernel Code (Ring 0)
        //   0x10 = Kernel Data (Ring 0)
        //   0x18 = User Code (Ring 3)
        //   0x20 = User Data (Ring 3)
        //
        // For SYSCALL (kernel entry): We want CS=0x08, SS=0x10
        //   Base = 0x08 -> CS=0x08, SS=0x10 ✓
        //
        // For SYSRET (user return): We want CS=0x1B (0x18|3), SS=0x23 (0x20|3)
        //   With base=0x10: CS=0x10+16=0x20, SS=0x10+8=0x18 (BACKWARDS!)
        //   With base=0x08: CS=0x08+16=0x18, SS=0x08+8=0x10 (SS is Kernel Data!)
        //
        // NOTE: Standard x86-64 GDT expects User Data BEFORE User Code for SYSRET!
        // Our current GDT has it reversed (Code at 0x18, Data at 0x20).
        // 
        // For now, we use IRETQ in JumpToUser which works with any GDT order.
        // SYSRET will need GDT reordering to work properly.
        
        uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x0008 << 32);
        wrmsr(MSR_STAR, star);
        wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
        wrmsr(MSR_SFMASK, 0x200);  // Mask IF flag during syscall
        
        EarlyTerm::Print("[Syscall] SYSCALL/SYSRET configured.\n");
    }
    
    void JumpToUser(void* entry, void* stack, void* arg1) {
        // Ring 3 segment selectors (selector value | RPL 3)
        uint64_t user_cs = 0x18 | 3;  // GDT entry 3, Ring 3
        uint64_t user_ss = 0x20 | 3;  // GDT entry 4, Ring 3
        uint64_t rflags = 0x202;      // IF (Interrupt Flag) set, reserved bit 1 set
        
        // ====================================================================
        // SERIAL TRACE - CRITICAL CHECKPOINT BEFORE IRETQ
        // If system reboots after this message, the fault is in the IRETQ
        // ====================================================================
        UART::Write("\n");
        UART::Write("====================================================\n");
        UART::Write("=== IRETQ CHECKPOINT - LAST MESSAGE BEFORE JUMP ===\n");
        UART::Write("====================================================\n");
        UART::Write("Entry Point (RIP): "); UART::WriteHex((uint64_t)entry); UART::Write("\n");
        UART::Write("User Stack (RSP):  "); UART::WriteHex((uint64_t)stack); UART::Write("\n");
        UART::Write("Arg1 (RDI):        "); UART::WriteHex((uint64_t)arg1); UART::Write("\n");
        UART::Write("User CS:           "); UART::WriteHex(user_cs); UART::Write("\n");
        UART::Write("User SS:           "); UART::WriteHex(user_ss); UART::Write("\n");
        UART::Write("RFLAGS:            "); UART::WriteHex(rflags); UART::Write("\n");
        UART::Write("\n");
        UART::Write(">>> EXECUTING IRETQ NOW <<<\n");
        UART::Write("If you see a Page Fault after this, the issue is:\n");
        UART::Write("  - Memory at Entry Point lacks PAGE_USER flag\n");
        UART::Write("  - Stack memory lacks PAGE_USER flag\n");
        UART::Write("  - GDT Ring 3 descriptors are misconfigured\n");
        UART::Write("====================================================\n\n");
        
        EarlyTerm::Print("[Syscall] Jumping to user mode...\n");
        
        // Setup RDI (First Argument) before IRETQ
        // IRETQ Frame on stack (from top to bottom):
        //   SS, RSP, RFLAGS, CS, RIP
        __asm__ volatile(
            "cli\n"                    // Disable interrupts
            "mov %5, %%rdi\n"          // Load arg1 into RDI (first argument)
            "mov %0, %%rax\n"          // SS
            "push %%rax\n"
            "mov %1, %%rax\n"          // RSP (user stack)
            "push %%rax\n"
            "mov %2, %%rax\n"          // RFLAGS
            "push %%rax\n"
            "mov %3, %%rax\n"          // CS
            "push %%rax\n"
            "mov %4, %%rax\n"          // RIP (entry point)
            "push %%rax\n"
            "iretq\n"                  // Return to Ring 3
            :
            : "r"(user_ss), "r"((uint64_t)stack), "r"(rflags), 
              "r"(user_cs), "r"((uint64_t)entry), "r"((uint64_t)arg1)
            : "rax", "rdi", "memory"
        );
    }

}

// Syscall handler - called from assembly
extern "C" uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (num) {
        case SYS_EXIT:
            return 0;
            
        case SYS_WRITE:
            for (uint64_t i = 0; i < arg3; i++) {
                EarlyTerm::PutChar(((char*)arg1)[i]);
            }
            return arg3;
            
        case SYS_MALLOC:
            return (uint64_t)kmalloc((size_t)arg1);
            
        case SYS_FREE:
            kfree((void*)arg1);
            return 0;
            
        case SYS_UPDATE_SCREEN:
            // Compose all layers and flip to framebuffer
            Compositor::Compose();
            Compositor::Flip();
            return 0;
            
        case SYS_GET_SCREEN_INFO:
            // Return packed width/height
            return ((uint64_t)Graphics::GetWidth() << 32) | Graphics::GetHeight();
            
        case SYS_BEEP:
            // Play beep: arg1 = frequency, arg2 = duration_ms
            Audio::Beep((uint32_t)arg1, (uint32_t)arg2);
            return 0;
            
        case 50: // SYS_VIDEO_MAP
            // Return pointer to framebuffer/backbuffer for direct access
            // Security: In a real microkernel, we would map a shared memory object.
            // Here we verify access (placeholder) and return the address.
            return (uint64_t)Graphics::GetFramebuffer();
            
        case 51: // SYS_VIDEO_FLIP
            // Trigger V-Sync / Flip
            Graphics::Flip();
            return 0;
            
        case 52: // SYS_INPUT_POLL
            // Arg1 = Pointer to InputEvent struct
            // Logic: Pop from input queue and copy to user buffer
            // For now, return 0 (no event)
            return 0;

        default:
            return (uint64_t)-1;
    }

}


