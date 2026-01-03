#include <stdint.h>
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/serial/uart.h"


namespace PIT {
    uint64_t ticks = 0;
    static uint32_t g_frequency_hz = 0;

    void Init(uint32_t frequency) {
        g_frequency_hz = frequency;
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
        // Heartbeat: ~1 line per second to prove IRQ0/ticks are alive.
        uint32_t hz = g_frequency_hz ? g_frequency_hz : 100;
        if ((ticks % hz) == 0) {
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
