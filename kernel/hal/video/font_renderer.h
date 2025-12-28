#pragma once

#include <stdint.h>

// PSF2 font format (PC Screen Font v2)
// Simple bitmap font format used by Linux console

struct PSF2Header {
    uint32_t magic;         // 0x864ab572
    uint32_t version;       // 0
    uint32_t headerSize;    // Size of header
    uint32_t flags;         // 0 = no unicode table
    uint32_t numGlyphs;     // Number of glyphs
    uint32_t bytesPerGlyph; // Size of each glyph
    uint32_t height;        // Height in pixels
    uint32_t width;         // Width in pixels
};

#define PSF2_MAGIC 0x864ab572

// Cached glyph for performance
struct CachedGlyph {
    uint32_t codepoint;
    uint8_t size;           // Font size (height)
    uint8_t width;
    uint8_t height;
    uint8_t* bitmap;        // Rendered bitmap
    CachedGlyph* next;      // LRU linked list
};

#define GLYPH_CACHE_SIZE 128

// Font Renderer - PSF2 support with glyph caching
namespace FontRenderer {
    // Initialize font renderer
    void Init();
    
    // Load PSF2 font from memory
    bool LoadFont(const uint8_t* data, uint32_t size);
    
    // Draw single character (returns width)
    uint32_t DrawChar(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                      uint32_t x, uint32_t y, uint32_t c, 
                      uint32_t color, uint32_t bg_color);
    
    // Draw string (returns total width)
    uint32_t DrawText(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                      uint32_t x, uint32_t y, const char* text,
                      uint32_t color, uint32_t bg_color);
    
    // Get text dimensions
    uint32_t GetTextWidth(const char* text);
    uint32_t GetFontHeight();
    
    // Cache management
    void ClearCache();
    uint32_t GetCacheHits();
    uint32_t GetCacheMisses();
}

// HAL interface for text drawing
void hal_draw_text(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                   uint32_t x, uint32_t y, const char* text,
                   uint32_t color, uint32_t size);
