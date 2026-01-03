#include <stdint.h>
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/serial/uart.h"


namespace PIT {
    uint64_t ticks = 0;

    void Init(uint32_t frequency) {
        uint32_t divisor = 1193180 / frequency;

        // Command: Channel 0, Access lo/hi, Mode 2 (Rate Generator), Binary
        IO::outb(0x43, 0x36);
        
        // Low byte
        IO::outb(0x40, (uint8_t)(divisor & 0xFF));
        // High byte
        IO::outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    }

    void OnInterrupt() {
        ticks++;

#ifdef MOUSE_DEBUG
        // Heartbeat: ~1 line per second at 100Hz to prove IRQ0/ticks are alive.
        if ((ticks % 100) == 0) {
            UART::Write("[PIT] tick=");
            UART::WriteDec((int64_t)ticks);
            UART::Write("\n");
        }
#endif
    }

    uint64_t GetTicks() {
        return ticks;
    }
}

// C-linkage wrapper for desktop.cpp
extern "C" uint64_t PIT_GetTicks() {
    return PIT::GetTicks();
}
