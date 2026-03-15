#pragma once

#include <stdint.h>
#include "../../shared/os_event.h" // For OSEvent struct

enum class TaskState {
    READY,      // Task is ready to run
    RUNNING,    // Task is currently executing
    SLEEPING    // Task is waiting (future use)
};

struct Task {
    uint64_t* stack_pointer; // Saved RSP
    uint64_t id;
    TaskState state;
    uint64_t cr3;            // Page Directory (CR3) for this task
    uint64_t wake_up_time; // For SLEEPING state (ticks)
    uint64_t kernel_stack_top; // TSS.RSP0: Thread-Specific Kernel Stack Top (for Interrupts/Syscalls)
    
    // Per-Task Event Queue (IPC)
    static const int EVENT_QUEUE_SIZE = 128;
    OSEvent eventQueue[EVENT_QUEUE_SIZE];
    volatile int eventHead = 0;
    volatile int eventTail = 0;

    Task* next;
    // Paging structures, priority, etc. can be added later
};

namespace Scheduler {
    void Init();
    
    // Create a new kernel thread
    void CreateTask(void (*entry_point)());
    
    // Create a new user thread with specific Page Table
    // arg1 is loaded into RDI for the new process (first argument)
    void CreateUserTask(void (*entry_point)(), void* user_stack, uint64_t cr3, uint64_t arg1 = 0);
    
    // Put current task to sleep for ms milliseconds
    void Sleep(uint64_t ms);
    
    // Called by Timer Interrupt (IRQ0)
    // Returns the new RSP to switch to (or the current one if no switch)
    uint64_t* Schedule(uint64_t* current_rsp);
    
    // Get current task ID (for debugging)
    // Get current task ID (for debugging)
    uint64_t GetCurrentTaskId();
    Task* GetCurrentTask();
    Task* GetTaskById(uint64_t id);

    // IPC
    bool PushEventToTask(uint64_t taskId, const OSEvent& ev);
    bool PopEventFromCurrentTask(OSEvent* outEv);
    uint64_t GetEventPushCount();
    uint64_t GetEventPopCount();
    uint64_t GetEventDropCount();
    
    // Yield current timeslice (voluntary switch)
    void Yield();
}
