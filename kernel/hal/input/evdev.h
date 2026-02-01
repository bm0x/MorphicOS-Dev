/**
 * evdev.h - Generic Event Device subsystem header
 * 
 * Linux evdev-compatible event buffer for input devices.
 */

#ifndef _EVDEV_H
#define _EVDEV_H

#include "input_event.h"
#include <stdint.h>

namespace Evdev {

/**
 * Initialize the evdev subsystem
 * Called once during kernel boot
 */
void Init();

/**
 * Push an event to the ring buffer
 * Called by device drivers (mouse, keyboard, etc.)
 * 
 * @param ev The event to push
 * @return true if successful, false if buffer is full
 */
bool PushEvent(const Input::InputEvent& ev);

/**
 * Pop an event from the ring buffer
 * Called by event consumers (compositor, input manager)
 * 
 * @param out Pointer to store the event
 * @return true if an event was retrieved, false if buffer is empty
 */
bool PopEvent(Input::InputEvent* out);

/**
 * Get the current number of events in the buffer
 */
uint32_t GetEventCount();

/**
 * Get total events processed since init
 */
uint64_t GetTotalEvents();

/**
 * Get number of events dropped due to buffer overflow
 */
uint64_t GetDroppedEvents();

// =============================================================================
// Helper Functions for Device Drivers
// =============================================================================

/**
 * Push relative mouse motion events (REL_X, REL_Y)
 * Automatically skips zero deltas
 */
void PushRelativeMotion(int32_t dx, int32_t dy);

/**
 * Push a button press/release event
 * @param button Button code (BTN_LEFT, BTN_RIGHT, etc.)
 * @param pressed true for press, false for release
 */
void PushButton(uint16_t button, bool pressed);

/**
 * Push a scroll wheel event
 * @param delta Scroll delta (positive = up, negative = down)
 */
void PushScroll(int32_t delta);

/**
 * Push a keyboard key event
 * @param keycode Key code (KEY_A, KEY_ENTER, etc.)
 * @param state KEY_PRESS, KEY_RELEASE, or KEY_REPEAT
 */
void PushKey(uint16_t keycode, int32_t state);

/**
 * Push a SYN_REPORT event to mark end of event batch
 * Call this after pushing a complete event packet
 */
void PushSync();

} // namespace Evdev

#endif // _EVDEV_H
