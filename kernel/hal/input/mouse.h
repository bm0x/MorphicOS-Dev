#pragma once

#include <stdint.h>

// Mouse event types
enum class MouseEventType {
    MOVE,
    BUTTON_DOWN,
    BUTTON_UP
};

// Mouse buttons
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

// Mouse event
struct MouseEvent {
    MouseEventType type;
    int16_t x, y;           // Current position
    int8_t dx, dy;          // Delta movement
    uint8_t buttons;        // Button state
};

// Mouse HAL - Cursor and input handling
namespace Mouse {
    // Initialize mouse driver
    void Init();
    
    // Update position (called from IRQ handler)
    void OnInterrupt();
    
    // Get current position
    int16_t GetX();
    int16_t GetY();
    
    // Get button state
    uint8_t GetButtons();
    bool IsLeftPressed();
    bool IsRightPressed();
    
    // Set screen bounds
    void SetBounds(uint16_t width, uint16_t height);
    
    // Cursor rendering
    void DrawCursor();
    void HideCursor();
    void SetCursorVisible(bool visible);
    
    // Poll for mouse event
    bool PollEvent(MouseEvent* event);
}
