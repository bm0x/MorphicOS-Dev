#include "graphics.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "early_term.h"
#include "alpha_lut.h"

// SIMD optimized memory operations (from blit_fast.S)
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
    void memset_fast_32(void* dest, uint32_t value, size_t count);
}

namespace Graphics {
    static FramebufferInfo* framebuffer = nullptr;
    static uint32_t* backbuffer = nullptr;
    static uint32_t width = 0;
    static uint32_t height = 0;
    static uint32_t pitch = 0;
    
    void Init(FramebufferInfo* fb) {
        framebuffer = fb;
        width = fb->width;
        height = fb->height;
        pitch = fb->pixelsPerScanLine;
        
        // Initialize Alpha LUT for fast blending
        Alpha::InitLUT();
        
        // Allocate backbuffer
        uint32_t bufferSize = pitch * height * sizeof(uint32_t);
        backbuffer = (uint32_t*)kmalloc(bufferSize);
        
        if (backbuffer) {
            // Use SIMD to clear buffer
            memset_fast_32(backbuffer, 0, pitch * height);
            EarlyTerm::Print("[Graphics] Double buffer: ");
            EarlyTerm::PrintDec(bufferSize / 1024);
            EarlyTerm::Print(" KB allocated.\n");
        } else {
            EarlyTerm::Print("[Graphics] WARNING: Backbuffer alloc failed!\n");
            backbuffer = (uint32_t*)fb->baseAddress;
        }
    }
    
    void Flip() {
        if (!framebuffer || !backbuffer) return;
        if (backbuffer == (uint32_t*)framebuffer->baseAddress) return;
        
        // SIMD copy: REP MOVSL (1 pixel per iteration)
        uint32_t* dest = (uint32_t*)framebuffer->baseAddress;
        blit_fast_32(dest, backbuffer, pitch * height);
    }
    
    void Clear(uint32_t color) {
        if (!backbuffer) return;
        // SIMD fill: REP STOSL
        memset_fast_32(backbuffer, color, pitch * height);
    }
    
    void PutPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!backbuffer || x >= width || y >= height) return;
        backbuffer[y * pitch + x] = color;
    }
    
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        if (!backbuffer) return;
        
        // Clamp to screen bounds
        if (x >= width || y >= height) return;
        uint32_t maxW = (x + w > width) ? (width - x) : w;
        uint32_t maxH = (y + h > height) ? (height - y) : h;
        
        // SIMD fill each row
        for (uint32_t row = 0; row < maxH; row++) {
            memset_fast_32(&backbuffer[(y + row) * pitch + x], color, maxW);
        }
    }
    
    void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        // Top and bottom lines (SIMD)
        if (y < height) {
            uint32_t lineW = (x + w > width) ? (width - x) : w;
            memset_fast_32(&backbuffer[y * pitch + x], color, lineW);
        }
        if (y + h - 1 < height) {
            uint32_t lineW = (x + w > width) ? (width - x) : w;
            memset_fast_32(&backbuffer[(y + h - 1) * pitch + x], color, lineW);
        }
        // Left and right (single pixels)
        for (uint32_t py = y; py < y + h && py < height; py++) {
            if (x < width) backbuffer[py * pitch + x] = color;
            if (x + w - 1 < width) backbuffer[py * pitch + x + w - 1] = color;
        }
    }
    
    void DrawImage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* data) {
        if (!backbuffer || !data) return;
        
        for (uint32_t py = 0; py < h && (y + py) < height; py++) {
            // Copy entire row if no alpha needed
            uint32_t copyW = (x + w > width) ? (width - x) : w;
            blit_fast_32(&backbuffer[(y + py) * pitch + x], &data[py * w], copyW);
        }
    }
    
    void DrawImageCentered(uint32_t w, uint32_t h, uint32_t* data) {
        uint32_t x = (width > w) ? (width - w) / 2 : 0;
        uint32_t y = (height > h) ? (height - h) / 2 : 0;
        DrawImage(x, y, w, h, data);
    }
    
    void BlendPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!backbuffer || x >= width || y >= height) return;
        
        uint8_t alpha = (color >> 24) & 0xFF;
        if (alpha == 0) return;
        if (alpha == 255) {
            backbuffer[y * pitch + x] = color;
            return;
        }
        
        // Fast alpha blend using LUT (no division!)
        uint32_t bg = backbuffer[y * pitch + x];
        backbuffer[y * pitch + x] = Alpha::Blend(color, bg);
    }
    
    // Partial flip - only copy specified rectangle (CRITICAL for performance)
    void FlipRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (!framebuffer || !backbuffer) return;
        if (backbuffer == (uint32_t*)framebuffer->baseAddress) return;
        
        // Clamp to screen
        if (x >= width || y >= height) return;
        if (x + w > width) w = width - x;
        if (y + h > height) h = height - y;
        
        uint32_t* dest = (uint32_t*)framebuffer->baseAddress;
        
        // Copy each row of the dirty rectangle
        for (uint32_t row = 0; row < h; row++) {
            uint32_t offset = (y + row) * pitch + x;
            blit_fast_32(&dest[offset], &backbuffer[offset], w);
        }
    }
    
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetPitch() { return pitch; }
    uint32_t* GetBackbuffer() { return backbuffer; }
}
