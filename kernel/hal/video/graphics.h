#pragma once

#include <stdint.h>
#include "../../../shared/boot_info.h"

// Graphics HAL - Double Buffering and Primitives
namespace Graphics {
    // Initialize graphics with framebuffer info
    void Init(FramebufferInfo* fb);
    
    // Double buffering
    void Flip();           // Copy entire backbuffer to framebuffer
    void FlipRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);  // Partial flip (dirty rect)
    void Clear(uint32_t color);
    
    // === ATOMIC COMPOSITION ===
    void FlipWithVSync();  // V-Blank wait + atomic flip
    void DrawCursorOnFramebuffer(int16_t x, int16_t y, const uint32_t* sprite, uint32_t w, uint32_t h);
    uint32_t* GetFramebuffer();  // Direct framebuffer access
    
    // Primitives (draw to backbuffer)
    void PutPixel(uint32_t x, uint32_t y, uint32_t color);
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    
    // Image rendering (BGRA format)
    void DrawImage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* data);
    void DrawImageCentered(uint32_t w, uint32_t h, uint32_t* data);
    
    // Alpha blending helper (R,G,B mixing)
    uint32_t BlendPixelRaw(uint32_t bg, uint32_t fg);
    
    // Alpha blending for transparency

    void BlendPixel(uint32_t x, uint32_t y, uint32_t color);
    
    // Get dimensions
    uint32_t GetWidth();
    uint32_t GetHeight();
    uint32_t GetPitch();
    
    // Direct access (for advanced use)
    uint32_t* GetBackbuffer();
}

// Color constants (BGRA format)
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFF0000FF
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFFFF0000
#define COLOR_YELLOW    0xFF00FFFF
#define COLOR_CYAN      0xFFFFFF00
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_GRAY      0xFF808080
#define COLOR_DARKGRAY  0xFF404040
