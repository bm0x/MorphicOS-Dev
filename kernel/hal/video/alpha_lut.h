#pragma once
// Alpha Blending Look-Up Table for Morphic OS Compositor
// Pre-computed alpha blending for O(1) transparency operations

#include <stdint.h>

namespace Alpha {
    // Pre-computed LUT: alpha_lut[alpha][color] = (alpha * color) >> 8
    // Avoids multiplication per pixel during blending
    static uint8_t lut[256][256];
    static bool initialized = false;
    
    // Initialize the lookup table (call once at startup)
    inline void InitLUT() {
        if (initialized) return;
        
        for (int a = 0; a < 256; a++) {
            for (int c = 0; c < 256; c++) {
                lut[a][c] = (uint8_t)((a * c) >> 8);
            }
        }
        initialized = true;
    }
    
    // Fast alpha blend using LUT
    // fg = foreground color (ARGB), bg = background color (ARGB)
    // Returns blended color
    inline uint32_t Blend(uint32_t fg, uint32_t bg) {
        uint8_t a = (fg >> 24) & 0xFF;
        
        // Fast path: fully opaque or transparent
        if (a == 255) return fg;
        if (a == 0) return bg;
        
        uint8_t inv_a = 255 - a;
        
        uint8_t r = lut[a][(fg >> 16) & 0xFF] + lut[inv_a][(bg >> 16) & 0xFF];
        uint8_t g = lut[a][(fg >> 8) & 0xFF] + lut[inv_a][(bg >> 8) & 0xFF];
        uint8_t b = lut[a][fg & 0xFF] + lut[inv_a][bg & 0xFF];
        
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Blend with custom alpha (ignores fg alpha channel)
    inline uint32_t BlendAlpha(uint32_t fg, uint32_t bg, uint8_t alpha) {
        if (alpha == 255) return fg;
        if (alpha == 0) return bg;
        
        uint8_t inv_a = 255 - alpha;
        
        uint8_t r = lut[alpha][(fg >> 16) & 0xFF] + lut[inv_a][(bg >> 16) & 0xFF];
        uint8_t g = lut[alpha][(fg >> 8) & 0xFF] + lut[inv_a][(bg >> 8) & 0xFF];
        uint8_t b = lut[alpha][fg & 0xFF] + lut[inv_a][bg & 0xFF];
        
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
    
    // Blend entire row of pixels (for compositor)
    inline void BlendRow(uint32_t* dest, uint32_t* src, int count, uint8_t alpha) {
        for (int i = 0; i < count; i++) {
            dest[i] = BlendAlpha(src[i], dest[i], alpha);
        }
    }
}
