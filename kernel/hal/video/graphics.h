#pragma once

#include <stdint.h>
#include "../../../shared/boot_info.h"

// Graphics HAL - Low Level Display Access & Primitives
// Now simplified to delegate buffer management to DRM

namespace Graphics {
    
    //=========================================================================
    // Initialization & Info
    //=========================================================================
    
    /**
     * Initialize graphics HAL with boot info
     */
    void Init(FramebufferInfo* fb);

    /**
     * Initialize DRM subsystem. Must be called AFTER KHeap::Init().
     */
    void InitDRM();

    uint32_t GetWidth();
    uint32_t GetHeight();
    uint32_t GetPitch();
    
    /**
     * Get direct VRAM pointer (Use with caution, prefer DRM)
     */
    uint32_t* GetVRAM();
    
    /**
     * Get current drawing target buffer
     * Default: VRAM (during boot) or Compositor Backbuffer (during userspace)
     */
    uint32_t* GetDrawBuffer();
    
    /**
     * Set drawing target
     * @param buffer Target buffer (nullptr = VRAM)
     */
    void SetDrawBuffer(uint32_t* buffer);

    //=========================================================================
    // Basic Primitives (Draw to current buffer)
    // Used for Boot Screen, Kernel Panic, etc.
    //=========================================================================
    
    void PutPixel(uint32_t x, uint32_t y, uint32_t color);
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    
    // Image rendering (BGRA format)
    void DrawImage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* data);
    void DrawImageCentered(uint32_t w, uint32_t h, uint32_t* data);
    
    // Alpha blending helper
    uint32_t BlendPixelRaw(uint32_t bg, uint32_t fg);
    void BlendPixel(uint32_t x, uint32_t y, uint32_t color);
    
    //=========================================================================
    // Hardware Acceleration / Low Level Blit
    //=========================================================================
    
    /**
     * Copy block of memory (SIMD optimized)
     */
    void BlitBuffer(uint32_t* dest, uint32_t* src, uint32_t count);

    //=========================================================================
    // Legacy / Compatibility (Deprecated)
    // These will now redirect to DRM or simple behaviors
    //=========================================================================
    void Flip(); 
    void FlipRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void ClearDirtyRects();
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
