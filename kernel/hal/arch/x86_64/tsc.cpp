#include "tsc.h"
#include "io.h"
#include "../../serial/uart.h"

namespace TSC {

    static uint64_t g_boot_tsc = 0;
    static uint64_t g_ticks_per_us = 2000; // Safe 2.0 GHz default fallback
    static bool g_initialized = false;

    void Init() {
        if (g_initialized) return;

        // Calibrate RDTSC using PIT Channel 2 (1,193,182 Hz) for ~10 ms (11932 ticks).
        // Works reliably even before IRQ0/interrupts are enabled.
        const uint16_t PIT_COUNT_10MS = 11932;

        // Gate speaker off, enable PIT Channel 2 gate (bit 0 = 1, bit 1 = 0)
        uint8_t port61 = IO::inb(0x61);
        IO::outb(0x61, (uint8_t)((port61 & 0xFD) | 0x01));

        // Channel 2, lobyte/hibyte, mode 0 (interrupt on terminal count)
        IO::outb(0x43, 0xB0);
        IO::outb(0x42, (uint8_t)(PIT_COUNT_10MS & 0xFF));
        IO::outb(0x42, (uint8_t)((PIT_COUNT_10MS >> 8) & 0xFF));

        uint64_t start = Read();

        // Wait until PIT Channel 2 output bit (port 0x61 bit 5) goes high
        uint32_t guard = 0;
        while ((IO::inb(0x61) & 0x20) == 0 && guard < 200000000u) {
            __asm__ volatile("pause");
            guard++;
        }

        uint64_t end = Read();

        // Restore port 0x61
        IO::outb(0x61, (uint8_t)(port61 & 0xFC));

        uint64_t delta = (end > start) ? (end - start) : 0;
        if (guard < 200000000u && delta >= 1000000ULL) {
            // delta ticks elapsed in 10,000 microseconds
            uint64_t tpus = delta / 10000ULL;
            if (tpus >= 100 && tpus <= 10000) {
                g_ticks_per_us = tpus;
            }
        }

        g_boot_tsc = Read();
        g_initialized = true;

        UART::Write("[TSC] Calibrated: ");
        UART::WriteDec((int64_t)g_ticks_per_us);
        UART::Write(" MHz (sub-microsecond clock ready)\n");
    }

    uint64_t GetMicroseconds() {
        if (!g_initialized) return 0;
        uint64_t now = Read();
        if (now <= g_boot_tsc) return 0;
        return (now - g_boot_tsc) / g_ticks_per_us;
    }

    uint64_t GetNanoseconds() {
        if (!g_initialized) return 0;
        uint64_t now = Read();
        if (now <= g_boot_tsc) return 0;
        uint64_t delta = now - g_boot_tsc;
        uint64_t us = delta / g_ticks_per_us;
        uint64_t rem = delta % g_ticks_per_us;
        return us * 1000ULL + (rem * 1000ULL) / g_ticks_per_us;
    }

    uint32_t GetFreqMHz() {
        return (uint32_t)g_ticks_per_us;
    }

    uint64_t GetTicksPerUs() {
        return g_ticks_per_us;
    }

    void DelayMicroseconds(uint64_t us) {
        if (!g_initialized || us == 0) return;
        uint64_t target = Read() + us * g_ticks_per_us;
        while (Read() < target) {
            __asm__ volatile("pause");
        }
    }

}
