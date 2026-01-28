#include "input_device.h"
#include "../video/early_term.h"
#include "../../process/scheduler.h"
#include "../platform.h"

namespace InputManager {
    // Maximum registered input devices
    static const int MAX_INPUT_DEVICES = 8;
    
    static IInputDevice* devices[MAX_INPUT_DEVICES];
    static uint32_t deviceCount = 0;

    // Small stash to buffer events until a compositor registers.
    static const int STASH_SIZE = 64;
    static OSEvent g_eventStash[STASH_SIZE];
    static int g_stashHead = 0;
    static int g_stashTail = 0;
    static int g_stashCount = 0;
    
    void Init() {
        for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
            devices[i] = nullptr;
        }
        deviceCount = 0;
        g_stashHead = 0;
        g_stashTail = 0;
        g_stashCount = 0;
        EarlyTerm::Print("[HAL] Input Manager Initialized.\n");
    }
    
    void RegisterDevice(IInputDevice* device) {
        if (!device) return;
        if (deviceCount >= MAX_INPUT_DEVICES) {
            EarlyTerm::Print("[HAL] ERROR: Max input devices reached!\n");
            return;
        }
        
        devices[deviceCount++] = device;
        
        // Initialize device
        if (device->init) {
            device->init();
        }
        
        EarlyTerm::Print("[HAL] Input: ");
        EarlyTerm::Print(device->name);
        EarlyTerm::Print(" registered.\n");
    }
    
    bool PollEvent(InputEvent* outEvent) {
        if (!outEvent) return false;
        
        // Poll all devices in priority order
        for (uint32_t i = 0; i < deviceCount; i++) {
            if (devices[i] && devices[i]->poll_event) {
                if (devices[i]->poll_event(outEvent)) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    uint32_t GetDeviceCount() {
        return deviceCount;
    }
    
    // === RING BUFFER IMPLEMENTATION ===
    // OLD: static EventRB eventBuffer;
    // NEW: Per-Process Queues in Scheduler
    static uint64_t g_compositorPID = 0;

    void SetCompositorPID(uint64_t pid) {
        bool ints = HAL::Platform::AreInterruptsEnabled();
        HAL::Platform::DisableInterrupts();
        g_compositorPID = pid;
        EarlyTerm::Print("[InputManager] Compositor Registered PID: ");
        EarlyTerm::PrintDec(pid);
        EarlyTerm::Print("\n");

        // Drain any stashed events into the compositor's queue.
        if (g_stashCount > 0 && g_compositorPID > 0) {
            EarlyTerm::Print("[InputManager] Draining stashed events to compositor: ");
            EarlyTerm::PrintDec(g_stashCount);
            EarlyTerm::Print("\n");
            while (g_stashCount > 0) {
                OSEvent ev = g_eventStash[g_stashTail];
                g_stashTail = (g_stashTail + 1) % STASH_SIZE;
                g_stashCount--;
                bool ok = Scheduler::PushEventToTask(g_compositorPID, ev);
                if (!ok) {
                    EarlyTerm::Print("[InputManager] Failed to push stashed event\n");
                    // If push fails (queue full), drop remaining stashed events.
                    g_stashCount = 0;
                    break;
                }
            }
        }
        if (ints) HAL::Platform::EnableInterrupts();
    }
    
    void PushEvent(const OSEvent& ev) {
        // Route to Compositor if registered
        if (g_compositorPID > 0) {
            bool ok = Scheduler::PushEventToTask(g_compositorPID, ev);
#ifdef MOUSE_DEBUG
            EarlyTerm::Print("[InputManager] PushEvent -> PID: ");
            EarlyTerm::PrintDec(g_compositorPID);
            EarlyTerm::Print(" result=");
            EarlyTerm::PrintDec(ok ? 1 : 0);
            EarlyTerm::Print("\n");
#endif
            if (!ok) {
                // Buffer full or task dead
                EarlyTerm::Print("[InputManager] Drop (Full)\n");
            }
        } else {
            // No compositor yet: stash events up to STASH_SIZE to avoid losing
            bool ints = HAL::Platform::AreInterruptsEnabled();
            HAL::Platform::DisableInterrupts();
            if (g_stashCount < STASH_SIZE) {
                g_eventStash[g_stashHead] = ev;
                g_stashHead = (g_stashHead + 1) % STASH_SIZE;
                g_stashCount++;
#ifdef MOUSE_DEBUG
                EarlyTerm::Print("[InputManager] Stashed event (no compositor) count="); EarlyTerm::PrintDec(g_stashCount); EarlyTerm::Print("\n");
#endif
            } else {
                EarlyTerm::Print("[InputManager] Drop (No Compositor & Stash Full)\n");
            }
            if (ints) HAL::Platform::EnableInterrupts();
        }
    }
    
    bool GetNextOSEvent(OSEvent* outEv) {
        // Pop from Current Task's Queue
        return Scheduler::PopEventFromCurrentTask(outEv);
    }

    // Called by IRQ dispatcher to notify all input devices
    void DispatchInterrupt(uint32_t irq) {
        // IRQ1 = Keyboard, IRQ12 = Mouse (PS/2)
#ifdef MOUSE_DEBUG
        EarlyTerm::Print("[InputManager] DispatchInterrupt irq="); EarlyTerm::PrintDec(irq); EarlyTerm::Print(" devCount="); EarlyTerm::PrintDec(deviceCount); EarlyTerm::Print("\n");
#endif
        for (uint32_t i = 0; i < deviceCount; i++) {
            if (devices[i] && devices[i]->on_interrupt) {
#ifdef MOUSE_DEBUG
                EarlyTerm::Print("[InputManager] Calling on_interrupt for device: "); EarlyTerm::Print(devices[i]->name); EarlyTerm::Print("\n");
#endif
                devices[i]->on_interrupt();
            }
        }
    }
}
