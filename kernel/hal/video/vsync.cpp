// V-Sync Implementation for Morphic OS - Optimized for 60 FPS
// Strategy: Fast detection + Timer-based fallback
// Goal: Never block for more than 2ms, maintain smooth frame pacing

#include "../arch/x86_64/io.h"
#include "graphics.h"

namespace VSync {

// VGA status register port
static const uint16_t VGA_STATUS_PORT = 0x3DA;
static const uint8_t VSYNC_BIT = 0x08;

static bool hardwareVSyncAvailable = false;
static bool initialized = false;

// Frame timing for software fallback
static uint64_t lastFrameTime = 0;
static const uint64_t FRAME_TIME_US = 16667; // ~60 Hz in microseconds

// External: PIT provides timing
namespace PIT {
extern uint64_t GetMicroseconds();
}

void Init() {
  if (initialized)
    return;

  // Quick detection: Check if VSync bit toggles within ~1ms
  // If port 0x3DA doesn't respond properly, don't use it

  const uint32_t DETECTION_TIMEOUT = 50000; // ~50k iterations = ~1ms
  uint32_t transitions = 0;
  uint8_t lastState = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;

  for (uint32_t i = 0; i < DETECTION_TIMEOUT; i++) {
    uint8_t current = IO::inb(VGA_STATUS_PORT) & VSYNC_BIT;
    if (current != lastState) {
      transitions++;
      lastState = current;
      if (transitions >= 2)
        break; // Detected at least one full cycle
    }
    __asm__ volatile("pause");
  }

  hardwareVSyncAvailable = (transitions >= 2);
  initialized = true;

  // Initialize timing
  lastFrameTime = 0; // Will be set on first frame
}

// Wait for vertical blank - Optimized
// Returns immediately if hardware VSync unavailable
// Uses short timeout to prevent blocking
void WaitForRetrace() {
  if (!initialized)
    Init();

  // === OPTION 1: Hardware VSync (fast path) ===
  if (hardwareVSyncAvailable) {
    // Short timeout: max ~2ms of waiting
    // At 60Hz, VBlank is ~1.3ms every 16.67ms
    const uint32_t MAX_WAIT_ITERATIONS = 100000;
    uint32_t count = 0;

    // Phase 1: Wait until we're NOT in retrace (exit current VBlank if any)
    while ((IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) && count < 5000) {
      __asm__ volatile("pause");
      count++;
    }

    // Phase 2: Wait until VBlank starts
    count = 0;
    while (!(IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) &&
           count < MAX_WAIT_ITERATIONS) {
      __asm__ volatile("pause");
      count++;
    }

    // If we hit timeout, hardware VSync might have failed
    // Fall through to software pacing
    if (count >= MAX_WAIT_ITERATIONS) {
      // Don't disable hardware VSync permanently - might be temporary
      // Just skip to software fallback this frame
    } else {
      return; // Success - VBlank detected
    }
  }

  // === OPTION 2: Software Frame Pacing (fallback) ===
  // Maintain consistent ~60 FPS using timer
  // This is more reliable than broken hardware VSync

  // For now, just return - the frame loop handles timing
  // The desktop's main loop already has frame pacing
}

// Non-blocking check if currently in retrace
bool IsInRetrace() {
  if (!hardwareVSyncAvailable)
    return false;
  return (IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) != 0;
}

// Get whether hardware VSync is working
bool IsHardwareVSyncAvailable() { return hardwareVSyncAvailable; }
} // namespace VSync
