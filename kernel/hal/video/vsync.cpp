// V-Sync Implementation for Morphic OS
// Synchronizes buffer swap with vertical retrace

#include "graphics.h"
#include "../arch/x86_64/io.h"

namespace VSync {
    
    // VGA status register port
    static const uint16_t VGA_STATUS_PORT = 0x3DA;
    
    // Bit 3: Vertical retrace in progress
    static const uint8_t VSYNC_BIT = 0x08;
    
    // Wait for vertical sync (eliminates tearing)
    void WaitForRetrace() {
        // Wait for end of current retrace (if in progress)
        while (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT);
        
        // Wait for start of next retrace
        while (!(IO::inb(VGA_STATUS_PORT) & VSYNC_BIT));
    }
    
    // Check if currently in vertical retrace
    bool IsInRetrace() {
        return (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) != 0;
    }
}
