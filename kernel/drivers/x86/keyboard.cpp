#include <stdint.h>
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/input/input_device.h"
#include "../../hal/input/keymap.h"
#include "../../hal/device_registry.h"
#include "../../hal/video/compositor.h"

// PS/2 Keyboard Driver
// Implements IInputDevice for HAL integration

namespace PS2Keyboard {
    // Internal event buffer
    static const int EVENT_BUFFER_SIZE = 64;
    static InputEvent eventBuffer[EVENT_BUFFER_SIZE];
    static volatile uint32_t read_ptr = 0;
    static volatile uint32_t write_ptr = 0;

    // US QWERTY Scancode Set 1 -> ASCII
    static const char scancode_table[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };

    // Low-level init (HAL function pointer)
    static void Init() {
        // Flush PS/2 buffer
        while (IO::inb(0x64) & 1) {
            IO::inb(0x60);
        }
    }

    // Modifier key state
    static bool shiftPressed = false;
    static bool capsLock = false;
    static bool ctrlPressed = false;
    static bool altPressed = false;

    // Interrupt handler (HAL function pointer)  
    static void OnInterrupt() {
        uint8_t status = IO::inb(0x64);
        if (!(status & 1)) return;
        
        uint8_t scancode = IO::inb(0x60);
        bool keyRelease = (scancode & 0x80) != 0;
        uint8_t keyCode = scancode & 0x7F;
        
        // Handle modifier keys
        if (keyCode == 0x2A || keyCode == 0x36) {  // Left/Right Shift
            shiftPressed = !keyRelease;
            return;
        }
        if (keyCode == 0x1D) {  // Ctrl
            ctrlPressed = !keyRelease;
            return;
        }
        if (keyCode == 0x38) {  // Alt
            altPressed = !keyRelease;
            return;
        }
        if (keyCode == 0x3A && !keyRelease) {  // Caps Lock toggle
            capsLock = !capsLock;
            return;
        }
        
        // F12 key (scancode 0x58) - Toggle debug overlay
        if (scancode == 0x58) {
            Compositor::ToggleDebugOverlay();
            Compositor::UpdateDebugOverlay();
        }
        
        // Create event
        InputEvent event;
        event.mouse_x = 0;
        event.mouse_y = 0;
        event.mouse_buttons = 0;
        event.keycode = keyCode;
        event.scancode_raw = scancode;
        
        if (keyRelease) {
            event.type = InputEventType::KEY_RELEASE;
            event.ascii = 0;
        } else {
            event.type = InputEventType::KEY_PRESS;
            // Use KeymapHAL for translation with shift/caps support
            uint16_t translated = KeymapHAL::Translate(keyCode, shiftPressed, altPressed, capsLock);
            event.ascii = (translated < 256) ? (char)translated : 0;
        }
        
        // Push OSEvent for Userspace
        OSEvent osEv;
        osEv.type = keyRelease ? OSEvent::KEY_RELEASE : OSEvent::KEY_PRESS;
        osEv.dx = 0;
        osEv.dy = 0;
        osEv.buttons = (shiftPressed ? 1 : 0) | (ctrlPressed ? 2 : 0) | (altPressed ? 4 : 0);
        osEv.scancode = keyCode;
        osEv.ascii = event.ascii;
        InputManager::PushEvent(osEv);

        // Add to buffer if there's space
        uint32_t next = (write_ptr + 1) % EVENT_BUFFER_SIZE;
        if (next != read_ptr) {
            eventBuffer[write_ptr] = event;
            write_ptr = next;
        }
    }

    // Poll for event (HAL function pointer)
    static bool PollEvent(InputEvent* out) {
        if (read_ptr == write_ptr) return false;
        
        *out = eventBuffer[read_ptr];
        read_ptr = (read_ptr + 1) % EVENT_BUFFER_SIZE;
        return true;
    }

    // Device descriptor
    static IInputDevice device = {
        "PS2Keyboard",
        Init,
        OnInterrupt,
        PollEvent
    };

    // Get device for registration
    IInputDevice* GetDevice() {
        return &device;
    }
}

// Legacy compatibility wrapper (for existing code)
namespace Keyboard {
    void Init() {
        DeviceRegistry::Register(DeviceType::INPUT, PS2Keyboard::GetDevice());
    }
    
    void OnInterrupt() {
        PS2Keyboard::GetDevice()->on_interrupt();
    }
    
    char GetChar() {
        InputEvent event;
        if (InputManager::PollEvent(&event)) {
            if (event.type == InputEventType::KEY_PRESS && event.ascii) {
                return event.ascii;
            }
        }
        return 0;
    }
}
