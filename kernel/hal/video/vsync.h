#pragma once

#include <stdint.h>

// V-Sync for smooth buffer swapping
namespace VSync {
// Initialize VSync detection
void Init();

// Wait for vertical retrace (eliminates tearing)
// Optimized: Returns quickly if hardware VSync unavailable
void WaitForRetrace();

// Check if currently in vertical retrace (non-blocking)
bool IsInRetrace();

// Check if hardware VSync is working
bool IsHardwareVSyncAvailable();
} // namespace VSync
