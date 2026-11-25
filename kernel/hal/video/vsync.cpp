// Native High-Refresh V-Sync Engine for Morphic OS (60 / 75 / 120 / 144 / 165 / 240 Hz)
// Uses Hardware VBlank when present and Phase-Locked Calibrated RDTSC VBlank otherwise.

#include "vsync.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/tsc.h"
#include "../platform.h"
#include "../serial/uart.h"
#include "../../process/scheduler.h"

namespace VSync {

    static constexpr uint16_t VGA_STATUS_PORT = 0x3DA;
    static constexpr uint8_t  VSYNC_BIT       = 0x08;
    static constexpr uint32_t MIN_REFRESH_HZ  = 30;
    static constexpr uint32_t MAX_REFRESH_HZ  = 240;

    static bool     g_hw_vsync_available = false;
    static bool     g_initialized        = false;
    static Mode     g_mode               = MODE_ADAPTIVE;
    static uint32_t g_refresh_hz         = 60;
    static uint32_t g_hw_detected_hz     = 60;
    static uint64_t g_target_frame_us    = 16667;

    static uint64_t g_next_vblank_us     = 0;
    static uint64_t g_last_present_us    = 0;
    static uint64_t g_last_frame_us      = 16667;
    static uint32_t g_measured_fps       = 60;
    static uint32_t g_fps_frames         = 0;
    static uint64_t g_fps_window_start_us = 0;

    static uint32_t SnapStandardHz(uint32_t raw_hz) {
        static const uint32_t kRates[] = {60, 75, 90, 120, 144, 165, 240};
        uint32_t best = 60;
        uint32_t bestDiff = 9999;
        for (uint32_t r : kRates) {
            uint32_t d = (raw_hz > r) ? (raw_hz - r) : (r - raw_hz);
            if (d < bestDiff) {
                bestDiff = d;
                best = r;
            }
        }
        return (bestDiff <= 8) ? best : raw_hz;
    }

    void Init() {
        if (g_initialized) return;

        // Probe hardware VGA retrace register over a bounded ~20ms window
        uint64_t start_us = TSC::GetMicroseconds();
        uint8_t lastState = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;
        uint64_t firstRiseUs = 0;
        uint64_t secondRiseUs = 0;
        uint32_t rises = 0;

        for (uint32_t i = 0; i < 400000; i++) {
            uint8_t cur = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;
            if (cur != lastState) {
                if (cur != 0) {
                    uint64_t now = TSC::GetMicroseconds();
                    if (rises == 0) firstRiseUs = now;
                    else if (rises == 1) {
                        secondRiseUs = now;
                        rises++;
                        break;
                    }
                    rises++;
                }
                lastState = cur;
            }
            if ((TSC::GetMicroseconds() - start_us) > 25000ULL) break;
            __asm__ volatile("pause");
        }

        if (rises >= 2 && secondRiseUs > firstRiseUs) {
            uint64_t period_us = secondRiseUs - firstRiseUs;
            if (period_us >= 3500ULL && period_us <= 35000ULL) {
                g_hw_vsync_available = true;
                uint32_t detected = (uint32_t)(1000000ULL / period_us);
                g_hw_detected_hz = SnapStandardHz(detected);
                g_refresh_hz = g_hw_detected_hz;
                g_target_frame_us = 1000000ULL / g_refresh_hz;
            }
        }

        uint64_t now = TSC::GetMicroseconds();
        g_next_vblank_us = now + g_target_frame_us;
        g_last_present_us = now;
        g_fps_window_start_us = now;
        g_initialized = true;

        UART::Write("[VSync] Initialized: ");
        UART::WriteDec((int64_t)g_refresh_hz);
        UART::Write("Hz (");
        UART::Write(g_hw_vsync_available ? "Hardware VBlank" : "Phase-Locked TSC VBlank");
        UART::Write(", max 240Hz)\n");
    }

    static void UpdateTelemetry(uint64_t now_us) {
        if (g_last_present_us > 0 && now_us > g_last_present_us) {
            g_last_frame_us = now_us - g_last_present_us;
        }
        g_last_present_us = now_us;

        g_fps_frames++;
        if (now_us >= g_fps_window_start_us + 500000ULL) {
            uint64_t elapsed_us = now_us - g_fps_window_start_us;
            if (elapsed_us > 0) {
                g_measured_fps = (uint32_t)((uint64_t)g_fps_frames * 1000000ULL / elapsed_us);
            }
            g_fps_frames = 0;
            g_fps_window_start_us = now_us;
        }
    }

    void WaitForRetrace() {
        if (!g_initialized) Init();

        uint64_t now = TSC::GetMicroseconds();
        if (g_mode == MODE_OFF) {
            UpdateTelemetry(now);
            return;
        }

        // Option 1: Real hardware VBlank when target Hz matches hardware display rate
        if (g_hw_vsync_available && g_refresh_hz == g_hw_detected_hz) {
            uint64_t guardStart = now;
            while ((IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) &&
                   (TSC::GetMicroseconds() - guardStart) < 3000ULL) {
                __asm__ volatile("pause");
            }
            while (!(IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) &&
                   (TSC::GetMicroseconds() - guardStart) < g_target_frame_us + 2000ULL) {
                __asm__ volatile("pause");
            }
            UpdateTelemetry(TSC::GetMicroseconds());
            return;
        }

        // Option 2: Precision Phase-Locked TSC VBlank (supports 60 / 75 / 120 / 144 / 165 / 240 Hz)
        if (now < g_next_vblank_us) {
            uint64_t remaining = g_next_vblank_us - now;

            // Clamp runaway deadline if caller was idle
            if (remaining > g_target_frame_us * 2) {
                g_next_vblank_us = now + g_target_frame_us;
                remaining = g_target_frame_us;
            }

            // Cooperative sleep for coarse milliseconds (keeps CPU usage low),
            // followed by sub-millisecond TSC spin for exact VBlank phase lock.
            if (remaining > 2200ULL && HAL::Platform::AreInterruptsEnabled()) {
                uint64_t sleep_ms = (remaining - 1500ULL) / 1000ULL;
                if (sleep_ms > 0) {
                    Scheduler::Sleep(sleep_ms);
                }
            }

            while (TSC::GetMicroseconds() < g_next_vblank_us) {
                __asm__ volatile("pause");
            }
        }

        uint64_t done = TSC::GetMicroseconds();
        // Advance phase grid cleanly without drift accumulation
        g_next_vblank_us += g_target_frame_us;
        if (g_next_vblank_us <= done) {
            g_next_vblank_us = done + g_target_frame_us;
        }

        UpdateTelemetry(done);
    }

    bool IsInRetrace() {
        if (!g_hw_vsync_available) return false;
        return (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) != 0;
    }

    bool IsHardwareVSyncAvailable() {
        return g_hw_vsync_available;
    }

    bool SetRefreshRate(uint32_t hz) {
        if (hz < MIN_REFRESH_HZ || hz > MAX_REFRESH_HZ) return false;
        g_refresh_hz = hz;
        g_target_frame_us = 1000000ULL / hz;
        uint64_t now = TSC::GetMicroseconds();
        g_next_vblank_us = now + g_target_frame_us;
        g_fps_frames = 0;
        g_fps_window_start_us = now;
        g_measured_fps = hz;
        g_last_frame_us = g_target_frame_us;
        return true;
    }

    uint32_t GetRefreshRate() { return g_refresh_hz; }
    uint32_t GetMaxRefreshRate() { return MAX_REFRESH_HZ; }
    uint64_t GetTargetFrameUs() { return g_target_frame_us; }

    void SetMode(Mode mode) {
        g_mode = mode;
        g_next_vblank_us = TSC::GetMicroseconds() + g_target_frame_us;
    }

    Mode GetMode() { return g_mode; }
    uint32_t GetMeasuredFps() { return g_measured_fps; }
    uint64_t GetLastFrameUs() { return g_last_frame_us; }

} // namespace VSync
