// V-Sync Implementation for Morphic OS
// Synchronizes buffer swap with vertical retrace

#include "graphics.h"
#include "../arch/x86_64/io.h"
#include "../../process/scheduler.h"

namespace VSync {
    
    // VGA status register port
    static const uint16_t VGA_STATUS_PORT = 0x3DA;
    // Bit 3: Vertical retrace in progress
    static const uint8_t VSYNC_BIT = 0x08;
    
    static bool hardwareVSyncAvailable = true;
    static bool initialized = false;

    void Init() {
        if (initialized) return;
        
        // Detection: Check if the VSync bit toggles within a reasonable timeframe.
        // If port 0x3DA is floating (0xFF) or fixed (0x00), loop won't exit properly w/o timeout.
        
        uint32_t timeout = 1000000;
        uint32_t transitions = 0;
        uint8_t initial = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;
        
        // Simple polling test
        for(uint32_t i = 0; i < timeout; i++) {
            uint8_t current = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;
            if (current != initial) {
                transitions++;
                initial = current;
                if (transitions >= 2) break; // Seen at least one full cycle (or part of it)
            }
        }
        
        if (transitions < 2) {
            hardwareVSyncAvailable = false;
            // EarlyTerm::Print("[VSync] Warning: Hardware VSync not detected (Port 0x3DA). Disabling.\n");
        } else {
             hardwareVSyncAvailable = true;
             // EarlyTerm::Print("[VSync] Hardware VSync Enabled.\n");
        }
        
        initialized = true;
    }

    // Wait for vertical sync (eliminates tearing)
    void WaitForRetrace() {
        if (!initialized) Init();
        if (!hardwareVSyncAvailable) return; // Fallback: Render immediately (tearing, but no hang)

        // Safety timeout for runtime waits (approx 1/60s frame is large, we use a loop count)
        const uint32_t SAFETY_TIMEOUT = 10000000;
        uint32_t counter = 0;

        // Wait for end of current retrace (if in progress)
        while (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) {
            counter++;
            if (counter > SAFETY_TIMEOUT) {
                 hardwareVSyncAvailable = false; // Disable dynamically if it fails
                 return;
            }
        }
        
        counter = 0;
        // Wait for start of next retrace
        // OPTIMIZATION: Yield to scheduler every N iterations to avoid hogging CPU
        uint32_t yieldCounter = 0;
        const uint32_t YIELD_INTERVAL = 1000; // Yield every 1000 iterations
        
        while (!(IO::inb(VGA_STATUS_PORT) & VSYNC_BIT)) {
            counter++;
            yieldCounter++;
            
            if (counter > SAFETY_TIMEOUT) {
                 // P1: Fallback to timer when HW VSync fails (Software VSync ~60 FPS)
                 Scheduler::Sleep(16);
                 return;
            }
            
            // Yield periodically to allow other tasks to run
            if (yieldCounter >= YIELD_INTERVAL) {
                Scheduler::Yield();
                yieldCounter = 0;
            }
            
            __asm__ volatile("pause");
        }
    }
    
    // Check if currently in vertical retrace
    bool IsInRetrace() {
        if (!hardwareVSyncAvailable) return false;
        return (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) != 0;
    }
}
