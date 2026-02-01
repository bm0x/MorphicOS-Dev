/**
 * input_event.h - Linux evdev-compatible input event structures
 * 
 * This header defines standardized input events compatible with Linux's evdev
 * subsystem. It provides a unified interface for all input devices (mouse,
 * keyboard, touchpad, etc.) following the Linux input protocol.
 * 
 * Reference: Linux kernel input.h and input-event-codes.h
 */

#ifndef _INPUT_EVENT_H
#define _INPUT_EVENT_H

#include <stdint.h>

namespace Input {

/**
 * Standardized input event structure
 * Compatible with Linux struct input_event
 */
struct InputEvent {
    uint64_t time_us;   // Timestamp in microseconds (from system timer)
    uint16_t type;      // Event type (EV_KEY, EV_REL, EV_ABS, etc.)
    uint16_t code;      // Event code (KEY_A, REL_X, BTN_LEFT, etc.)
    int32_t  value;     // Event value (key state, axis delta, etc.)
};

// =============================================================================
// Event Types (subset of Linux EV_* constants)
// =============================================================================
enum EventType : uint16_t {
    EV_SYN       = 0x00,   // Synchronization event (marks end of event batch)
    EV_KEY       = 0x01,   // Key/button state change
    EV_REL       = 0x02,   // Relative axis change (mouse movement)
    EV_ABS       = 0x03,   // Absolute axis change (touchpad, tablet)
    EV_MSC       = 0x04,   // Miscellaneous events
    EV_SW        = 0x05,   // Switch events (lid, headphone jack)
    EV_LED       = 0x11,   // LED control
    EV_REP       = 0x14,   // Autorepeat
    EV_MAX       = 0x1F
};

// =============================================================================
// Relative Axis Codes (REL_*)
// =============================================================================
enum RelCode : uint16_t {
    REL_X        = 0x00,   // X axis (horizontal movement)
    REL_Y        = 0x01,   // Y axis (vertical movement)
    REL_Z        = 0x02,   // Z axis (rarely used)
    REL_RX       = 0x03,   // X rotation
    REL_RY       = 0x04,   // Y rotation
    REL_RZ       = 0x05,   // Z rotation
    REL_HWHEEL   = 0x06,   // Horizontal scroll wheel
    REL_DIAL     = 0x07,   // Dial
    REL_WHEEL    = 0x08,   // Vertical scroll wheel
    REL_MISC     = 0x09,
    REL_MAX      = 0x0F
};

// =============================================================================
// Button Codes (BTN_*)
// Linux uses codes starting at 0x100 for buttons
// =============================================================================
enum BtnCode : uint16_t {
    // Mouse buttons
    BTN_MISC     = 0x100,
    BTN_0        = 0x100,
    
    BTN_MOUSE    = 0x110,
    BTN_LEFT     = 0x110,   // Left mouse button
    BTN_RIGHT    = 0x111,   // Right mouse button
    BTN_MIDDLE   = 0x112,   // Middle mouse button
    BTN_SIDE     = 0x113,   // Side button (extra mouse button)
    BTN_EXTRA    = 0x114,   // Extra button
    BTN_FORWARD  = 0x115,   // Forward navigation
    BTN_BACK     = 0x116,   // Back navigation
    BTN_TASK     = 0x117,   // Task button
    
    // Touch/stylus
    BTN_TOUCH    = 0x14A,
    BTN_STYLUS   = 0x14B,
    BTN_STYLUS2  = 0x14C
};

// =============================================================================
// Key Codes (KEY_*) - Common subset
// Full keyboard scan codes following USB HID
// =============================================================================
enum KeyCode : uint16_t {
    KEY_RESERVED = 0,
    KEY_ESC      = 1,
    KEY_1        = 2,
    KEY_2        = 3,
    KEY_3        = 4,
    KEY_4        = 5,
    KEY_5        = 6,
    KEY_6        = 7,
    KEY_7        = 8,
    KEY_8        = 9,
    KEY_9        = 10,
    KEY_0        = 11,
    KEY_MINUS    = 12,
    KEY_EQUAL    = 13,
    KEY_BACKSPACE = 14,
    KEY_TAB      = 15,
    KEY_Q        = 16,
    KEY_W        = 17,
    KEY_E        = 18,
    KEY_R        = 19,
    KEY_T        = 20,
    KEY_Y        = 21,
    KEY_U        = 22,
    KEY_I        = 23,
    KEY_O        = 24,
    KEY_P        = 25,
    KEY_LEFTBRACE  = 26,
    KEY_RIGHTBRACE = 27,
    KEY_ENTER    = 28,
    KEY_LEFTCTRL = 29,
    KEY_A        = 30,
    KEY_S        = 31,
    KEY_D        = 32,
    KEY_F        = 33,
    KEY_G        = 34,
    KEY_H        = 35,
    KEY_J        = 36,
    KEY_K        = 37,
    KEY_L        = 38,
    KEY_SEMICOLON = 39,
    KEY_APOSTROPHE = 40,
    KEY_GRAVE    = 41,
    KEY_LEFTSHIFT = 42,
    KEY_BACKSLASH = 43,
    KEY_Z        = 44,
    KEY_X        = 45,
    KEY_C        = 46,
    KEY_V        = 47,
    KEY_B        = 48,
    KEY_N        = 49,
    KEY_M        = 50,
    KEY_COMMA    = 51,
    KEY_DOT      = 52,
    KEY_SLASH    = 53,
    KEY_RIGHTSHIFT = 54,
    KEY_KPASTERISK = 55,
    KEY_LEFTALT  = 56,
    KEY_SPACE    = 57,
    KEY_CAPSLOCK = 58,
    KEY_F1       = 59,
    KEY_F2       = 60,
    KEY_F3       = 61,
    KEY_F4       = 62,
    KEY_F5       = 63,
    KEY_F6       = 64,
    KEY_F7       = 65,
    KEY_F8       = 66,
    KEY_F9       = 67,
    KEY_F10      = 68,
    KEY_F11      = 87,
    KEY_F12      = 88,
    
    // Arrow keys
    KEY_UP       = 103,
    KEY_LEFT     = 105,
    KEY_RIGHT    = 106,
    KEY_DOWN     = 108,
    
    // Special
    KEY_HOME     = 102,
    KEY_END      = 107,
    KEY_PAGEUP   = 104,
    KEY_PAGEDOWN = 109,
    KEY_INSERT   = 110,
    KEY_DELETE   = 111
};

// =============================================================================
// Synchronization Codes (SYN_*)
// =============================================================================
enum SynCode : uint16_t {
    SYN_REPORT   = 0,    // Marks end of event packet
    SYN_CONFIG   = 1,    // Configuration change
    SYN_MT_REPORT = 2,   // Multitouch report
    SYN_DROPPED  = 3     // Events were dropped (buffer overflow)
};

// =============================================================================
// Event Value Constants
// =============================================================================
constexpr int32_t KEY_RELEASE = 0;   // Key/button released
constexpr int32_t KEY_PRESS   = 1;   // Key/button pressed
constexpr int32_t KEY_REPEAT  = 2;   // Key held (autorepeat)

} // namespace Input

#endif // _INPUT_EVENT_H
