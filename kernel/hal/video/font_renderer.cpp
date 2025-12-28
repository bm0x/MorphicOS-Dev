#include "font_renderer.h"
#include "early_term.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"

// Reference to font.cpp (must be outside namespace)
extern const uint8_t font8x16[256 * 16];

namespace FontRenderer {
    static PSF2Header* fontHeader = nullptr;
    static uint8_t* glyphData = nullptr;
    
    // Simple glyph cache
    static CachedGlyph cache[GLYPH_CACHE_SIZE];
    static uint32_t cacheHits = 0;
    static uint32_t cacheMisses = 0;
    
    // Built-in 8x16 font (fallback)
    static bool useBuiltinFont = true;
    static uint32_t fontWidth = 8;
    static uint32_t fontHeight = 16;
    
    void Init() {

        fontHeader = nullptr;
        glyphData = nullptr;
        useBuiltinFont = true;
        
        for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
            cache[i].codepoint = 0xFFFFFFFF;
            cache[i].bitmap = nullptr;
        }
        
        EarlyTerm::Print("[FontRenderer] Using built-in 8x16 font.\n");
    }
    
    bool LoadFont(const uint8_t* data, uint32_t size) {
        if (size < sizeof(PSF2Header)) return false;
        
        PSF2Header* header = (PSF2Header*)data;
        if (header->magic != PSF2_MAGIC) {
            EarlyTerm::Print("[FontRenderer] Invalid PSF2 magic.\n");
            return false;
        }
        
        fontHeader = header;
        glyphData = (uint8_t*)(data + header->headerSize);
        fontWidth = header->width;
        fontHeight = header->height;
        useBuiltinFont = false;
        
        EarlyTerm::Print("[FontRenderer] Loaded PSF2: ");
        EarlyTerm::PrintDec(header->numGlyphs);
        EarlyTerm::Print(" glyphs, ");
        EarlyTerm::PrintDec(fontWidth);
        EarlyTerm::Print("x");
        EarlyTerm::PrintDec(fontHeight);
        EarlyTerm::Print("\n");
        
        return true;
    }
    
    // Get glyph bitmap from built-in font
    static const uint8_t* GetBuiltinGlyph(uint32_t c) {
        if (c >= 256) c = '?';
        return &font8x16[c * 16];  // Each char is 16 bytes
    }
    
    // Get glyph bitmap from PSF2 font
    static const uint8_t* GetPSF2Glyph(uint32_t c) {
        if (!fontHeader || !glyphData) return nullptr;
        if (c >= fontHeader->numGlyphs) c = '?';
        return glyphData + c * fontHeader->bytesPerGlyph;
    }
    
    uint32_t DrawChar(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                      uint32_t x, uint32_t y, uint32_t c, 
                      uint32_t color, uint32_t bg_color) {
        const uint8_t* glyph;
        uint32_t w = fontWidth;
        uint32_t h = fontHeight;
        
        if (useBuiltinFont) {
            glyph = GetBuiltinGlyph(c);
            w = 8;
            h = 16;
        } else {
            glyph = GetPSF2Glyph(c);
        }
        
        if (!glyph) return 0;
        
        // Draw glyph to buffer
        for (uint32_t py = 0; py < h && (y + py) < buf_h; py++) {
            uint8_t row = glyph[py];
            for (uint32_t px = 0; px < w && (x + px) < buf_w; px++) {
                uint32_t pcolor = (row & (0x80 >> px)) ? color : bg_color;
                if (pcolor != 0) {  // 0 = transparent
                    buffer[(y + py) * buf_w + (x + px)] = pcolor;
                }
            }
        }
        
        return w;
    }
    
    uint32_t DrawText(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                      uint32_t x, uint32_t y, const char* text,
                      uint32_t color, uint32_t bg_color) {
        uint32_t startX = x;
        
        while (*text) {
            if (*text == '\n') {
                x = startX;
                y += fontHeight;
            } else {
                x += DrawChar(buffer, buf_w, buf_h, x, y, *text, color, bg_color);
            }
            text++;
        }
        
        return x - startX;
    }
    
    uint32_t GetTextWidth(const char* text) {
        return kstrlen(text) * fontWidth;
    }
    
    uint32_t GetFontHeight() {
        return fontHeight;
    }
    
    void ClearCache() {
        for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
            if (cache[i].bitmap) {
                kfree(cache[i].bitmap);
                cache[i].bitmap = nullptr;
            }
            cache[i].codepoint = 0xFFFFFFFF;
        }
    }
    
    uint32_t GetCacheHits() { return cacheHits; }
    uint32_t GetCacheMisses() { return cacheMisses; }
}

// HAL interface
void hal_draw_text(uint32_t* buffer, uint32_t buf_w, uint32_t buf_h,
                   uint32_t x, uint32_t y, const char* text,
                   uint32_t color, uint32_t size) {
    // Size parameter ignored for now (fixed size font)
    FontRenderer::DrawText(buffer, buf_w, buf_h, x, y, text, color, 0);
}
