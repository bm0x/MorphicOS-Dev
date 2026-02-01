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

// Cursor overlay constants
#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16
#define CURSOR_BUFFER_SIZE (CURSOR_WIDTH * CURSOR_HEIGHT)

// === CURSOR STATE MACHINE ===
enum class CursorVisibility {
    HIDDEN,         // No cursor rendered (text shell)
    VISIBLE_TEXT,   // Text mode cursor (blinking underscore)
    VISIBLE_GUI     // GUI mode cursor (arrow sprite)
};

// === VISUAL CONTEXT SYSTEM ===
enum class VisualContext {
    TEXT_SHELL,     // Text-only terminal mode
    GRAPHICAL_GUI   // Desktop environment mode
};

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
    
    // Update position (called from IRQ handler) - FAST PATH ENABLED
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
    
    // === CURSOR STATE MACHINE ===
    void SetVisibility(CursorVisibility state);
    CursorVisibility GetVisibility();
    
    // Refresh cursor even when static (call from main loop)
    void RefreshCursor();
    
    // === VISUAL CONTEXT SYSTEM ===
    void SetVisualContext(VisualContext mode);
    VisualContext GetVisualContext();
    
    // === ZERO-LATENCY CURSOR SYSTEM ===
    
    // Enable/disable fast path rendering (IRQ-driven)
    void EnableFastPath(bool enable);
    
    // Initialize overlay system with backbuffer pointer
    void InitOverlay(uint32_t* backbuffer, uint32_t pitch);
    
    // Save background under cursor position
    void SaveBackground(int16_t x, int16_t y);
    
    // Restore previously saved background
    void RestoreBackground();
    
    // Draw cursor immediately (used in IRQ)
    void DrawCursorFast();
    
    // === POST-COMPOSITION CURSOR ===
    // Draw cursor directly on framebuffer AFTER flip (no flicker)
    // Asynchronous Atomic version using Scratchpad
    void RenderCursorAtomic();

    
    // Get cursor sprite for external drawing
    const uint32_t* GetCursorSprite();
    
    // Legacy cursor rendering (for non-fast-path mode)
    // Draw cursor directly to buffer
    void DrawCursor(uint32_t* buffer, uint32_t width, uint32_t height);
    void HideCursor();
    void SetCursorVisible(bool visible);
    
    // Poll for mouse event
    bool PollEvent(MouseEvent* event);
}

