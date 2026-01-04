#pragma once

#include <stdint.h>

// MSR addresses for SYSCALL/SYSRET
#define MSR_STAR    0xC0000081  // Segment selectors
#define MSR_LSTAR   0xC0000082  // Syscall entry point (RIP)
#define MSR_SFMASK  0xC0000084  // RFLAGS mask

// Syscall numbers
#define SYS_EXIT           0
#define SYS_WRITE          1
#define SYS_READ           2
#define SYS_MALLOC         3
#define SYS_FREE           4
#define SYS_UPDATE_SCREEN  10   // Compose and flip
#define SYS_GET_SCREEN_INFO 11  // Get screen dimensions
#define SYS_BEEP           12   // Play beep (freq, duration_ms)
#define SYS_SLEEP          13   // Sleep (ms)
#define SYS_GET_TIME_MS    20   // Get system time in milliseconds
#define SYS_GET_EVENT      21   // Get next input event (OSEvent)
#define SYS_ALLOC_BACKBUFFER 53 // Allocate and map userspace backbuffer
#define SYS_SPAWN 60
#define SYS_DEBUG_PRINT 61



namespace Syscall {
    // Initialize SYSCALL/SYSRET mechanism via MSRs
    void Init();
    
    // Jump to user mode (Ring 3)
    // entry: User code entry point
    // stack: User stack pointer
    // arg1: First argument (RDI)
    void JumpToUser(void* entry, void* stack, void* arg1);

}

// C handler called from assembly
extern "C" uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
