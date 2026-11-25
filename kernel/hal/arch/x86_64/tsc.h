#pragma once
#include <stdint.h>

namespace TSC {
    // Calibrate CPU Timestamp Counter (RDTSC) against hardware PIT Channel 2
    void Init();

    // Raw 64-bit RDTSC counter
    static inline uint64_t Read() {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }

    // Calibrated monotonic time since TSC::Init()
    uint64_t GetMicroseconds();
    uint64_t GetNanoseconds();

    // Calibrated frequency in MHz (e.g. 2400) and ticks per microsecond
    uint32_t GetFreqMHz();
    uint64_t GetTicksPerUs();

    // Sub-millisecond busy-wait using calibrated RDTSC
    void DelayMicroseconds(uint64_t us);
}
