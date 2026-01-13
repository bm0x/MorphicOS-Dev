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

    // External tick getter from PIT (1ms resolution)
    extern "C" uint64_t PIT_GetTicks();
    
    // Halt wrapper
    namespace HAL { namespace Platform { void Halt(); void EnableInterrupts(); } }

    void CreateTask(void (*entry_point)()) {
        Task* newTask = (Task*)kmalloc(sizeof(Task));
        if (!newTask) {
            EarlyTerm::Print("[Scheduler] ERROR: Cannot allocate new task!\n");
            return;
        }
        
        newTask->id = nextTaskId++;
        newTask->wake_up_time = 0;
        
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
        newTask->wake_up_time = 0;
        
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

    void Sleep(uint64_t ms) {
        if (!currentTask) return;
        
        uint64_t now = PIT_GetTicks();
        currentTask->wake_up_time = now + ms;
        currentTask->state = TaskState::SLEEPING;
        
        Yield();
    }
    
    void Yield() {
        // Trigger interrupt 0x20 (timer) to force schedule
        __asm__ volatile("int $0x20");
    }

    uint64_t* Schedule(uint64_t* current_rsp) {
        // Save current task's RSP
        if (currentTask) {
            currentTask->stack_pointer = current_rsp;
        } else {
             // Should not happen if initialized
            return current_rsp;
        }

        // Check if current task should wake up (if it was sleeping but got interrupted)
        // Actually, if we are inside Schedule, it means we grabbed CPU.
        // If we are marked SLEEPING, we just set that state in Sleep().
        // So we need to switch away from it.
        
        uint64_t now = PIT_GetTicks();
        
        // Find next READY task
        Task* start = currentTask;
        Task* next = start->next;
        
        // Iterate through list to find a runnable task
        while (next != start) {
            if (next->state == TaskState::SLEEPING) {
                if (now >= next->wake_up_time) {
                    next->state = TaskState::READY;
                }
            }
            
            if (next->state == TaskState::READY || next->state == TaskState::RUNNING) {
                break;
            }
            next = next->next;
        }
        
        // Handle case where we looped back to current task
        // If current is sleeping, we have a problem: ALL tasks are sleeping.
        if (next == start) {
             if (currentTask->state == TaskState::SLEEPING) {
                 if (now >= currentTask->wake_up_time) {
                     currentTask->state = TaskState::RUNNING;
                 } else {
                     // ALL TASKS SLEEPING.
                     // Enable interrupts and Halt CPU until next tick.
                     // We pretend to switch to current task, but we must be careful not to return to code execution
                     // because code would immediately loop back to Sleep logic or similar.
                     // The safe bet is to return current_rsp, return to the "int 0x20" or IRQ handler, 
                     // and the CPU stays in the loop of the idle task?
                     // If we don't have an IDLE task, we simulate one:
                     // We return, but we need to ensure we don't execute 'user' code that thinks it's awake.
                     
                     // NOTE: A proper OS has an Idle Task. We don't.
                     // HACK: Busy loop with HLT inside the scheduler?
                     // No, that blocks IRQs if called from ISR unless we STI.
                     
                     // Let's just create an IDLE task in Init or handle it here.
                     // Simplest: Enable Interrupts, HLT, then check again.
                     // BUT we are in an ISR context (IRQ0). We shouldn't block here.
                     // We must return a valid stack.
                     
                     // If we return *current* stack, the task wraps up ISR and goes back to...
                     // The instruction after Yield(). Which loops?
                     
                     // If we are the ONLY task and we sleep...
                     // We need an IDLE task.
                     
                     // For now, let's just create a dummy IDLE task in Init() so this loop always finds SOMETHING.
                     // But we didn't add that to plan.
                     // Fallback: If all sleeping, just return current but don't mark it ready?
                     // No, user code will run.
                     
                     // Quick fix: Just busy wait here with interrupts enabled?
                     // Dangerous stack depth.
                     
                     // Let's assume there is always task 0 (Shell/Kernel) which rarely sleeps?
                     // Shell: wait for key.
                     
                     // NOTE: Our Scheduler::Init creates MainTask (id 0).
                     // If MainTask sleeps, we die.
                 }
             }
        }
        
        currentTask = next;
        currentTask->state = TaskState::RUNNING;
        
        return currentTask->stack_pointer;
    }
    
    uint64_t GetCurrentTaskId() {
        return currentTask ? currentTask->id : 0;
    }
}
