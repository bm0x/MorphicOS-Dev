#include "scheduler.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"
#include "../hal/arch/x86_64/tss.h"
#include "../hal/platform.h"

extern "C" uint64_t kernel_stack_top; // Boot stack symbol

namespace Scheduler {
    Task* currentTask = nullptr;
    Task* tasksHead = nullptr;
    Task* idleTask = nullptr;  // Special idle task
    uint64_t nextTaskId = 1;
    static volatile uint64_t g_eventPushCount = 0;
    static volatile uint64_t g_eventPopCount = 0;
    static volatile uint64_t g_eventDropCount = 0;
    
    // Idle Task Entry Point - runs when no other tasks are ready
    // Uses HLT to reduce CPU power consumption and heat
    static void IdleTaskEntry() {
        while (true) {
            // Enable interrupts and halt until next interrupt
            // This saves power and reduces CPU heat
            __asm__ volatile(
                "sti\n\t"   // Enable interrupts
                "hlt\n\t"   // Halt until interrupt
                : : : "memory"
            );
        }
    }
    
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
        // CRITICAL: Disable interrupts during scheduler initialization
        // to prevent timer IRQ from calling Schedule() before we're ready
        __asm__ volatile("cli");
        
        Task* mainTask = (Task*)kmalloc(sizeof(Task));
        if (!mainTask) {
            EarlyTerm::Print("[Scheduler] FATAL: Cannot allocate main task!\n");
            while(1);
        }
        
        mainTask->id = 0;
        mainTask->stack_pointer = nullptr; // Will be set on first Schedule call
        // Ensure event queue indices are initialized for the main task
        mainTask->eventHead = 0;
        mainTask->eventTail = 0;
        
        // Main task runs in the boot page table
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        mainTask->cr3 = cr3;
        
        // Main Task uses the Boot Stack
        mainTask->kernel_stack_top = (uint64_t)&kernel_stack_top;
        
        mainTask->state = TaskState::RUNNING; // It's already running!
        mainTask->next = mainTask; // Circular list
        
        currentTask = mainTask;
        tasksHead = mainTask;
        
        EarlyTerm::Print("[Scheduler] Initialized. Main Task ID: 0\n");
        
        // Create the Idle Task - runs when all other tasks are sleeping
        // This prevents 100% CPU usage when system is idle
        CreateTask(IdleTaskEntry);
        idleTask = tasksHead->next;  // The task we just created
        idleTask->state = TaskState::READY;
        EarlyTerm::Print("[Scheduler] Idle Task created for power saving\n");
        
        // Re-enable interrupts now that scheduler is fully initialized
        __asm__ volatile("sti");
    }

    // External tick getter from PIT (1ms resolution)
    extern "C" uint64_t PIT_GetTicks();
    
    // Halt wrapper
    // namespace HAL { namespace Platform { void Halt(); void EnableInterrupts(); } }

    void CreateTask(void (*entry_point)()) {
        Task* newTask = (Task*)kmalloc(sizeof(Task));
        if (!newTask) {
            EarlyTerm::Print("[Scheduler] ERROR: Cannot allocate new task!\n");
            return;
        }
        kmemset(newTask, 0, sizeof(Task));
        
        newTask->id = nextTaskId++;
        newTask->wake_up_time = 0;
        
        // Use current CR3 for kernel tasks
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        newTask->cr3 = cr3;
        
        // Allocate Stack (16KB)
        uint64_t* stackBase = (uint64_t*)kmalloc(16384);
        if (!stackBase) {
            EarlyTerm::Print("[Scheduler] ERROR: Cannot allocate stack!\n");
            kfree(newTask);
            return;
        }
        kmemset(stackBase, 0, 16384);  // Zero the stack for safety
        
        // Stack layout (grows downward):
        // [stackBase + 16384] = absolute top
        // [stackBase + 16384 - sizeof(StackFrame)] = StackFrame for iretq
        // The task's RSP after iretq should point above StackFrame
        
        uint64_t stackTop = (uint64_t)stackBase + 16384;
        
        // Reserve space for StackFrame at the very top
        uint64_t* framePtr = (uint64_t*)(stackTop - sizeof(StackFrame));
        
        StackFrame* frame = (StackFrame*)framePtr;
        kmemset(frame, 0, sizeof(StackFrame));
        
        frame->rip = (uint64_t)entry_point;
        frame->cs = 0x08; // Kernel Code Segment
        frame->rflags = 0x202; // IF=1, Reserved=1
        
        // After iretq, RSP will be loaded from here
        // Point to the area BELOW the StackFrame (where task can push)
        frame->rsp = (uint64_t)framePtr;  // Task uses space below the frame
        frame->ss = 0x10; // Kernel Data Segment
        
        newTask->stack_pointer = (uint64_t*)framePtr;
        newTask->kernel_stack_top = stackTop;  // TSS needs the actual top
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

    void CreateUserTask(void (*entry_point)(), void* user_stack, uint64_t cr3, uint64_t arg1) {
        Task* newTask = (Task*)kmalloc(sizeof(Task));
        if (!newTask) return;
        kmemset(newTask, 0, sizeof(Task));
        
        newTask->id = nextTaskId++;
        newTask->wake_up_time = 0;
        newTask->cr3 = cr3;
        
        // Allocate Kernel Stack for this task (used when interrupt occurs in Ring 3)
        uint64_t* kstackBase = (uint64_t*)kmalloc(16384);
        if (!kstackBase) { kfree(newTask); return; }
        
        uint64_t* kstackTop = (uint64_t*)((uint64_t)kstackBase + 16384);
        
        // Setup Stack Frame for "iretq"
        kstackTop = (uint64_t*)((uint64_t)kstackTop - sizeof(StackFrame));
        
        StackFrame* frame = (StackFrame*)kstackTop;
        kmemset(frame, 0, sizeof(StackFrame));
        
        frame->rip = (uint64_t)entry_point;
        frame->cs = 0x23; // User Code (RPL 3)
        frame->rflags = 0x202; // IF=1
        frame->rsp = (uint64_t)user_stack;
        frame->ss = 0x1B; // User Data (RPL 3)
        // Set initial registers for the user task. The userspace entry expects
        // the first argument in RDI (System V). Place arg1 into the saved RDI
        // so that when the IRETQ/entry happens, RDI contains this value.
        frame->rdi = arg1;
        
        newTask->stack_pointer = (uint64_t*)kstackTop;
        newTask->kernel_stack_top = (uint64_t)kstackBase + 16384; // Store original top for TSS
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
                     // ALL TASKS SLEEPING - Switch to Idle Task
                     // The Idle Task uses HLT instruction to save CPU power
                     // It will be preempted when next timer interrupt fires
                     if (idleTask && idleTask != currentTask) {
                         next = idleTask;
                         next->state = TaskState::RUNNING;
                     }
                     // If idleTask unavailable, current task continues (rare)
                 }
             }
        }
        
        currentTask = next;
        currentTask->state = TaskState::RUNNING;
        
        // Context Switch: Page Table
        // Only switch if different to avoid TLB flush penalty
        uint64_t currentCR3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(currentCR3));
        
        if (currentTask->cr3 != (currentCR3 & ~0xFFF)) {
            // EarlyTerm::Print("[Sched] Switch CR3\n");
            __asm__ volatile("mov %0, %%cr3" : : "r"(currentTask->cr3) : "memory");
        }
        
        // CRITICAL: Update TSS RSP0 so next Interrupt/Syscall uses THIS task's stack
        TSS::SetKernelStack(currentTask->kernel_stack_top);
        
        return currentTask->stack_pointer;
    }
    
    uint64_t GetCurrentTaskId() {
        return currentTask ? currentTask->id : 0;
    }

    Task* GetCurrentTask() {
        return currentTask;
    }

    Task* GetTaskById(uint64_t id) {
        if (!tasksHead) return nullptr;
        Task* t = tasksHead;
        do {
            if (t->id == id) return t;
            t = t->next;
        } while (t != tasksHead);
        return nullptr;
    }

    bool PushEventToTask(uint64_t taskId, const OSEvent& ev) {
        Task* t = GetTaskById(taskId);
        if (!t) return false;

        // Use lock? (We are single core for now, but interrupt safety needed)
        bool ints = HAL::Platform::AreInterruptsEnabled();
        HAL::Platform::DisableInterrupts();

        int next = (t->eventHead + 1) % Task::EVENT_QUEUE_SIZE;
        if (next == t->eventTail) {
            g_eventDropCount++;
            // Full
    #ifdef MOUSE_DEBUG
            EarlyTerm::Print("[Scheduler] PushEventToTask FULL for PID: ");
            EarlyTerm::PrintDec(taskId);
            EarlyTerm::Print(" head="); EarlyTerm::PrintDec(t->eventHead);
            EarlyTerm::Print(" tail="); EarlyTerm::PrintDec(t->eventTail);
            EarlyTerm::Print("\n");
    #endif
            if (ints) HAL::Platform::EnableInterrupts();
            return false;
        }

        // Place event
        t->eventQueue[t->eventHead] = ev;
        t->eventHead = next;
        g_eventPushCount++;
    #ifdef MOUSE_DEBUG
        EarlyTerm::Print("[Scheduler] PushEventToTask OK for PID: ");
        EarlyTerm::PrintDec(taskId);
        EarlyTerm::Print(" head="); EarlyTerm::PrintDec(t->eventHead);
        EarlyTerm::Print(" tail="); EarlyTerm::PrintDec(t->eventTail);
        EarlyTerm::Print("\n");
    #endif
        
        // Wake up task if sleeping (ensure event consumer runs promptly)
        if (t->state == TaskState::SLEEPING) {
            t->state = TaskState::READY;
        }
        
        if (ints) ::HAL::Platform::EnableInterrupts();
        return true;
    }

    bool PopEventFromCurrentTask(OSEvent* outEv) {
        Task* t = currentTask;
        if (!t || !outEv) return false;

        bool ints = ::HAL::Platform::AreInterruptsEnabled();
        ::HAL::Platform::DisableInterrupts();

        if (t->eventHead == t->eventTail) {
            if (ints) ::HAL::Platform::EnableInterrupts();
            return false;
        }

        *outEv = t->eventQueue[t->eventTail];
        t->eventTail = (t->eventTail + 1) % Task::EVENT_QUEUE_SIZE;
        g_eventPopCount++;

        if (ints) ::HAL::Platform::EnableInterrupts();
        return true;
    }

    uint64_t GetEventPushCount() {
        return g_eventPushCount;
    }

    uint64_t GetEventPopCount() {
        return g_eventPopCount;
    }

    uint64_t GetEventDropCount() {
        return g_eventDropCount;
    }
}
