#pragma once
#include <stdint.h>

namespace HAL {
    struct Platform {
        // Initialize all core hardware (GDT, IDT, Serial, Interrupts)
        static void Init();

        // Enable global interrupts
        static void EnableInterrupts();

        // Disable global interrupts
        static void DisableInterrupts();
        
        // Check if interrupts are enabled
        static bool AreInterruptsEnabled();

        // Halt the CPU (wait for interrupt)
        static void Halt();
        
        // Reboot the system
        static void Reboot();
    };
}
