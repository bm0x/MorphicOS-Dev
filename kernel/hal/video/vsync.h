#pragma once

#include <stdint.h>

// Native High-Refresh V-Sync Engine (60 / 75 / 120 / 144 / 165 / 240 Hz + Adaptive)
namespace VSync {

    enum Mode : uint32_t {
        MODE_ADAPTIVE = 0, // Hardware VBlank when available, else Phase-Locked TSC VBlank
        MODE_HARDWARE = 1, // Prefer hardware retrace register when available
        MODE_OFF      = 2  // Unlocked / Tear-allowed low-latency mode
    };

    void Init();

    // Wait for next VBlank edge (hardware or precision TSC phase-locked)
    void WaitForRetrace();

    bool IsInRetrace();
    bool IsHardwareVSyncAvailable();

    // High-refresh configuration (30..240 Hz)
    bool SetRefreshRate(uint32_t hz);
    uint32_t GetRefreshRate();
    uint32_t GetMaxRefreshRate();
    uint64_t GetTargetFrameUs();

    void SetMode(Mode mode);
    Mode GetMode();

    // Live frame telemetry
    uint32_t GetMeasuredFps();
    uint64_t GetLastFrameUs();
}
