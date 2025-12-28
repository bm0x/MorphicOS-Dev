#include "syscall.h"
#include "../../video/early_term.h"
#include "../../video/compositor.h"
#include "../../video/graphics.h"
#include "../../audio/audio_device.h"
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
        uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x0008 << 32);
        wrmsr(MSR_STAR, star);
        wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
        wrmsr(MSR_SFMASK, 0x200);
        
        EarlyTerm::Print("[Syscall] SYSCALL/SYSRET configured.\n");
    }
    
    void JumpToUser(void* entry, void* stack) {
        uint64_t user_cs = 0x18 | 3;
        uint64_t user_ss = 0x20 | 3;
        uint64_t rflags = 0x202;
        
        EarlyTerm::Print("[Syscall] Jumping to user mode...\n");
        
        __asm__ volatile(
            "cli\n"
            "mov %0, %%rax\n"
            "push %%rax\n"
            "mov %1, %%rax\n"
            "push %%rax\n"
            "mov %2, %%rax\n"
            "push %%rax\n"
            "mov %3, %%rax\n"
            "push %%rax\n"
            "mov %4, %%rax\n"
            "push %%rax\n"
            "iretq\n"
            :
            : "r"(user_ss), "r"((uint64_t)stack), "r"(rflags), 
              "r"(user_cs), "r"((uint64_t)entry)
            : "rax", "memory"
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
            
        default:
            return (uint64_t)-1;
    }
}


