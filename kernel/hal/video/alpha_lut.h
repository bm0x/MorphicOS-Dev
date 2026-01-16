#pragma once
// Alpha Blending Look-Up Table for Morphic OS Compositor
// Pre-computed alpha blending for O(1) transparency operations
//
// Variables are declared extern here and defined in alpha_lut.cpp

#include <stdint.h>

namespace Alpha {
    // Extern declarations - defined in one .cpp file
    extern uint8_t* lut;
    extern bool initialized;
    
    // Initialize the lookup table (call once at startup in Graphics::Init)
    void InitLUT();
    
    // Fast alpha blend using LUT
    // fg = foreground color (ARGB), bg = background color (ARGB)
    // Returns blended color
    inline uint32_t Blend(uint32_t fg, uint32_t bg) {
        if (!initialized || !lut) return fg; // Fallback if not initialized
        
        uint8_t a = (fg >> 24) & 0xFF;
        
        // Fast path: fully opaque or transparent
        if (a == 255) return fg;
        if (a == 0) return bg;
        
        uint8_t inv_a = 255 - a;
        
        uint8_t r = lut[a * 256 + ((fg >> 16) & 0xFF)] + lut[inv_a * 256 + ((bg >> 16) & 0xFF)];
        uint8_t g = lut[a * 256 + ((fg >> 8) & 0xFF)] + lut[inv_a * 256 + ((bg >> 8) & 0xFF)];
        uint8_t b = lut[a * 256 + (fg & 0xFF)] + lut[inv_a * 256 + (bg & 0xFF)];
        
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Blend with custom alpha (ignores fg alpha channel)
    inline uint32_t BlendAlpha(uint32_t fg, uint32_t bg, uint8_t alpha) {
        if (!initialized || !lut) return fg;
        
        if (alpha == 255) return fg;
        if (alpha == 0) return bg;
        
        uint8_t inv_a = 255 - alpha;
        
        uint8_t r = lut[alpha * 256 + ((fg >> 16) & 0xFF)] + lut[inv_a * 256 + ((bg >> 16) & 0xFF)];
        uint8_t g = lut[alpha * 256 + ((fg >> 8) & 0xFF)] + lut[inv_a * 256 + ((bg >> 8) & 0xFF)];
        uint8_t b = lut[alpha * 256 + (fg & 0xFF)] + lut[inv_a * 256 + (bg & 0xFF)];
        
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Blend entire row of pixels (for compositor)
    inline void BlendRow(uint32_t* dest, uint32_t* src, int count, uint8_t alpha) {
        for (int i = 0; i < count; i++) {
            dest[i] = BlendAlpha(src[i], dest[i], alpha);
        }
    }
}
