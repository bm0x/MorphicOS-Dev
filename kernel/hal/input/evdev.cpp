/**
 * evdev.cpp - Generic Event Device subsystem (Linux evdev-inspired)
 * 
 * This module provides a centralized event buffer for all input devices.
 * It follows the Linux evdev model where:
 * 1. Device drivers produce standardized InputEvent structs
 * 2. Events are stored in a ring buffer
 * 3. Consumers (compositor, userspace) read events from the buffer
 * 
 * Benefits:
 * - Decouples device drivers from consumers
 * - Provides timestamping for all events
 * - Supports batching of events (SYN_REPORT)
 * - Large buffer prevents event loss during high load
 */

#include "evdev.h"
#include "input_event.h"
#include "../platform.h"

// =============================================================================
// RDTSC - High Resolution Time Stamp Counter
// =============================================================================
// RDTSC reads the CPU's internal cycle counter directly.
// Benefits over PIT:
// - Much higher resolution (nanoseconds vs milliseconds)
// - Faster to read (single CPU instruction vs memory access)
// - Monotonically increasing
//
// Note: For precise microseconds, we'd need CPU frequency calibration.
// For evdev ordering purposes, raw TSC values are sufficient.
// =============================================================================

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// For evdev, we use raw TSC values as timestamps
// They're monotonic and high-resolution, perfect for event ordering
static inline uint64_t GetTimestamp() {
    return rdtsc();
}

namespace Evdev {

// =============================================================================
// Ring Buffer Configuration
// =============================================================================

// Large buffer to prevent event loss (4096 events ~= 32KB)
// This is much larger than the old 64-event stash
static constexpr uint32_t BUFFER_SIZE = 4096;

static Input::InputEvent ringBuffer[BUFFER_SIZE];
static volatile uint32_t head = 0;   // Write position (producer)
static volatile uint32_t tail = 0;   // Read position (consumer)
static volatile uint32_t count = 0;  // Current event count

// Statistics for debugging/monitoring
static volatile uint64_t totalEvents = 0;
static volatile uint64_t droppedEvents = 0;

// =============================================================================
// Core API
// =============================================================================

void Init() {
    head = 0;
    tail = 0;
    count = 0;
    totalEvents = 0;
    droppedEvents = 0;
}

bool PushEvent(const Input::InputEvent& ev) {
    // Disable interrupts for atomic access
    bool ints = HAL::Platform::AreInterruptsEnabled();
    HAL::Platform::DisableInterrupts();
    
    // Check for buffer full
    if (count >= BUFFER_SIZE) {
        droppedEvents++;
        if (ints) HAL::Platform::EnableInterrupts();
        return false;
    }
    
    // Store event
    ringBuffer[head] = ev;
    head = (head + 1) % BUFFER_SIZE;
    count++;
    totalEvents++;
    
    if (ints) HAL::Platform::EnableInterrupts();
    return true;
}

bool PopEvent(Input::InputEvent* out) {
    if (!out) return false;
    
    // Disable interrupts for atomic access
    bool ints = HAL::Platform::AreInterruptsEnabled();
    HAL::Platform::DisableInterrupts();
    
    if (count == 0) {
        if (ints) HAL::Platform::EnableInterrupts();
        return false;
    }
    
    *out = ringBuffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    count--;
    
    if (ints) HAL::Platform::EnableInterrupts();
    return true;
}

uint32_t GetEventCount() {
    return count;
}

uint64_t GetTotalEvents() {
    return totalEvents;
}

uint64_t GetDroppedEvents() {
    return droppedEvents;
}

// =============================================================================
// Helper Functions for Device Drivers
// =============================================================================

void PushRelativeMotion(int32_t dx, int32_t dy) {
    uint64_t now = GetTimestamp();
    
    if (dx != 0) {
        PushEvent({now, Input::EV_REL, Input::REL_X, dx});
    }
    if (dy != 0) {
        PushEvent({now, Input::EV_REL, Input::REL_Y, dy});
    }
}

void PushButton(uint16_t button, bool pressed) {
    uint64_t now = GetTimestamp();
    PushEvent({now, Input::EV_KEY, button, pressed ? Input::KEY_PRESS : Input::KEY_RELEASE});
}

void PushScroll(int32_t delta) {
    uint64_t now = GetTimestamp();
    PushEvent({now, Input::EV_REL, Input::REL_WHEEL, delta});
}

void PushKey(uint16_t keycode, int32_t state) {
    uint64_t now = GetTimestamp();
    PushEvent({now, Input::EV_KEY, keycode, state});
}

void PushSync() {
    uint64_t now = GetTimestamp();
    PushEvent({now, Input::EV_SYN, Input::SYN_REPORT, 0});
}

} // namespace Evdev
