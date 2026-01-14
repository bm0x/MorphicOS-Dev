#pragma once

#include <stdint.h>

// Input Event Types
enum class InputEventType {
    KEY_PRESS,
    KEY_RELEASE,
    MOUSE_MOVE,
    MOUSE_BUTTON_DOWN,
    MOUSE_BUTTON_UP
};

// Standardized Input Event
struct InputEvent {
    InputEventType type;
    uint32_t keycode;      // Scancode for keyboard events
    uint8_t scancode_raw;  // Raw scancode (for F12, etc)
    char ascii;            // ASCII translation (0 if none)
    int16_t mouse_x;       // Mouse movement delta
    int16_t mouse_y;
    uint8_t mouse_buttons; // Button state bitmask
};


// Forward declaration
struct IInputDevice;

// Function pointer types for speed
typedef void (*InputInitFunc)();
typedef void (*InputInterruptFunc)();
typedef bool (*InputPollFunc)(InputEvent* outEvent);

// Input Device Interface
// Drivers implement this to register with HAL
struct IInputDevice {
    char name[32];
    
    // Function pointers (HAL pattern - ultra-fast)
    InputInitFunc init;
    InputInterruptFunc on_interrupt;
    InputPollFunc poll_event;
};

#include "../../../shared/os_event.h"

// Input Manager - Central event dispatcher
namespace InputManager {
    void Init();
    
    // Register an input device
    void RegisterDevice(IInputDevice* device);
    
    // Poll for next event from any device (Internal HAL polling)
    bool PollEvent(InputEvent* outEvent);
    
    // === NEW EVENT PIPELINE (Ring 3 Support) ===
    // Push an event to the global Kernel Ring Buffer
    void PushEvent(const OSEvent& ev);
    
    // Pop an event for the Syscall
    // Pop an event for the Syscall
    bool GetNextOSEvent(OSEvent* outEv);

    // Compositor Registration
    void SetCompositorPID(uint64_t pid);
    
    // Get registered device count
    uint32_t GetDeviceCount();
}
