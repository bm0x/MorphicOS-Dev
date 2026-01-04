#include "scheduler.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

namespace Scheduler {
    Task* currentTask = nullptr;
    Task* tasksHead = nullptr;
    uint64_t nextTaskId = 1;
    
    // Structure of the stack pushed by ISR and Common Stub
    // In x64 Long Mode, CPU ALWAYS pushes SS and RSP (even for same-privilege)
    struct StackFrame {
        // Pushed by Common Stub
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
        
        // Pushed by ISR Macro
        uint64_t interrupt_number;
        uint64_t error_code;
        
        // Pushed by CPU (x64 Long Mode ALWAYS pushes all 5)
        uint64_t rip;
        uint64_t cs;
        uint64_t rflags;
        uint64_t rsp;  // Original RSP before interrupt
        uint64_t ss;   // Original SS before interrupt
    } __attribute__((packed));

    void Init() {
        Task* mainTask = (Task*)kmalloc(sizeof(Task));
        if (!mainTask) {
            EarlyTerm::Print("[Scheduler] FATAL: Cannot allocate main task!\n");
            while(1);
        }
        
        mainTask->id = 0;
        mainTask->stack_pointer = nullptr; // Will be set on first Schedule call
        mainTask->state = TaskState::RUNNING; // It's already running!
        mainTask->next = mainTask; // Circular list
        
        currentTask = mainTask;
        tasksHead = mainTask;
        
        EarlyTerm::Print("[Scheduler] Initialized. Main Task ID: 0\n");
    }

    void CreateTask(void (*entry_point)()) {
        Task* newTask = (Task*)kmalloc(sizeof(Task));
        if (!newTask) {
            EarlyTerm::Print("[Scheduler] ERROR: Cannot allocate new task!\n");
            return;
        }
        
        newTask->id = nextTaskId++;
        
        // Allocate Stack (4KB)
        uint64_t* stackBase = (uint64_t*)kmalloc(4096);
        if (!stackBase) {
            EarlyTerm::Print("[Scheduler] ERROR: Cannot allocate stack!\n");
            kfree(newTask);
            return;
        }
        
        uint64_t* stackTop = (uint64_t*)((uint64_t)stackBase + 4096);
        
        // Setup Stack Frame for "iretq"
        stackTop = (uint64_t*)((uint64_t)stackTop - sizeof(StackFrame));
        
        StackFrame* frame = (StackFrame*)stackTop;
        kmemset(frame, 0, sizeof(StackFrame));
        
        frame->rip = (uint64_t)entry_point;
        frame->cs = 0x08; // Kernel Code Segment
        frame->rflags = 0x202; // IF=1, Reserved=1
        
        // CRITICAL for x64: Set RSP and SS for iretq
        // After iretq, CPU will load RSP from here (task's own stack top)
        frame->rsp = (uint64_t)stackBase + 4096; // Top of allocated stack
        frame->ss = 0x10; // Kernel Data Segment
        
        newTask->stack_pointer = (uint64_t*)stackTop;
        newTask->state = TaskState::READY;
        
        // Insert into circular list after current head
        newTask->next = tasksHead->next;
        tasksHead->next = newTask;
        
        EarlyTerm::Print("[Scheduler] Created Task ID: ");
        EarlyTerm::PrintDec(newTask->id);
        EarlyTerm::Print(" StackTop: ");
        EarlyTerm::PrintHex((uint64_t)stackTop);
        EarlyTerm::Print("\n");
    }

    void CreateUserTask(void (*entry_point)(), void* user_stack) {
        Task* newTask = (Task*)kmalloc(sizeof(Task));
        if (!newTask) return;
        
        newTask->id = nextTaskId++;
        
        // Allocate Kernel Stack for this task (used when interrupt occurs in Ring 3)
        uint64_t* kstackBase = (uint64_t*)kmalloc(4096);
        if (!kstackBase) { kfree(newTask); return; }
        
        uint64_t* kstackTop = (uint64_t*)((uint64_t)kstackBase + 4096);
        
        // Setup Stack Frame for "iretq"
        kstackTop = (uint64_t*)((uint64_t)kstackTop - sizeof(StackFrame));
        
        StackFrame* frame = (StackFrame*)kstackTop;
        kmemset(frame, 0, sizeof(StackFrame));
        
        frame->rip = (uint64_t)entry_point;
        frame->cs = 0x23; // User Code (RPL 3)
        frame->rflags = 0x202; // IF=1
        frame->rsp = (uint64_t)user_stack;
        frame->ss = 0x1B; // User Data (RPL 3)
        
        newTask->stack_pointer = (uint64_t*)kstackTop;
        newTask->state = TaskState::READY;
        
        newTask->next = tasksHead->next;
        tasksHead->next = newTask;
        
        EarlyTerm::Print("[Scheduler] Created User Task ID: ");
        EarlyTerm::PrintDec(newTask->id);
        EarlyTerm::Print("\n");
    }

    uint64_t* Schedule(uint64_t* current_rsp) {
        if (!currentTask) return current_rsp;
        
        // Save current task's RSP
        currentTask->stack_pointer = current_rsp;
        
        // If there's only one task, don't switch
        if (currentTask->next == currentTask) {
            return current_rsp;
        }
        
        // Mark current task as READY (it was RUNNING)
        currentTask->state = TaskState::READY;
        
        // Find next READY task (Round Robin)
        Task* next = currentTask->next;
        
        // Simple round-robin: just go to next
        currentTask = next;
        currentTask->state = TaskState::RUNNING;
        
        return currentTask->stack_pointer;
    }
    
    uint64_t GetCurrentTaskId() {
        return currentTask ? currentTask->id : 0;
    }
}
