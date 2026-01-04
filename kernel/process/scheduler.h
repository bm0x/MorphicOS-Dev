#pragma once

#include <stdint.h>

enum class TaskState {
    READY,      // Task is ready to run
    RUNNING,    // Task is currently executing
    SLEEPING    // Task is waiting (future use)
};

struct Task {
    uint64_t* stack_pointer; // Saved RSP
    uint64_t id;
    TaskState state;
    Task* next;
    // Paging structures, priority, etc. can be added later
};

namespace Scheduler {
    void Init();
    
    // Create a new kernel thread
    void CreateTask(void (*entry_point)());
    
    // Create a new user thread
    void CreateUserTask(void (*entry_point)(), void* user_stack);
    
    // Called by Timer Interrupt (IRQ0)
    // Returns the new RSP to switch to (or the current one if no switch)
    uint64_t* Schedule(uint64_t* current_rsp);
    
    // Get current task ID (for debugging)
    uint64_t GetCurrentTaskId();
}
