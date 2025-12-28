#include "graphics.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "early_term.h"

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
        
        // Allocate backbuffer
        uint32_t bufferSize = pitch * height * sizeof(uint32_t);
        backbuffer = (uint32_t*)kmalloc(bufferSize);
        
        if (backbuffer) {
            kmemset(backbuffer, 0, bufferSize);
            EarlyTerm::Print("[Graphics] Double buffer: ");
            EarlyTerm::PrintDec(bufferSize / 1024);
            EarlyTerm::Print(" KB allocated.\n");
        } else {
            EarlyTerm::Print("[Graphics] WARNING: Backbuffer alloc failed!\n");
            // Fallback: draw directly to framebuffer
            backbuffer = (uint32_t*)fb->baseAddress;
        }
    }
    
    void Flip() {
        if (!framebuffer || !backbuffer) return;
        if (backbuffer == (uint32_t*)framebuffer->baseAddress) return;
        
        // Fast copy backbuffer to framebuffer
        uint32_t* dest = (uint32_t*)framebuffer->baseAddress;
        uint32_t size = pitch * height;
        
        // Use optimized copy (could be memcpy or rep movsq)
        kmemcpy(dest, backbuffer, size * sizeof(uint32_t));
    }
    
    void Clear(uint32_t color) {
        if (!backbuffer) return;
        
        uint32_t size = pitch * height;
        for (uint32_t i = 0; i < size; i++) {
            backbuffer[i] = color;
        }
    }
    
    void PutPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!backbuffer || x >= width || y >= height) return;
        backbuffer[y * pitch + x] = color;
    }
    
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        if (!backbuffer) return;
        
        for (uint32_t py = y; py < y + h && py < height; py++) {
            for (uint32_t px = x; px < x + w && px < width; px++) {
                backbuffer[py * pitch + px] = color;
            }
        }
    }
    
    void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        // Top and bottom
        for (uint32_t px = x; px < x + w && px < width; px++) {
            if (y < height) PutPixel(px, y, color);
            if (y + h - 1 < height) PutPixel(px, y + h - 1, color);
        }
        // Left and right
        for (uint32_t py = y; py < y + h && py < height; py++) {
            if (x < width) PutPixel(x, py, color);
            if (x + w - 1 < width) PutPixel(x + w - 1, py, color);
        }
    }
    
    void DrawImage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* data) {
        if (!backbuffer || !data) return;
        
        for (uint32_t py = 0; py < h && (y + py) < height; py++) {
            for (uint32_t px = 0; px < w && (x + px) < width; px++) {
                uint32_t color = data[py * w + px];
                // Skip transparent pixels (alpha = 0)
                if ((color >> 24) != 0) {
                    backbuffer[(y + py) * pitch + (x + px)] = color;
                }
            }
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
        
        // Alpha blending
        uint32_t bg = backbuffer[y * pitch + x];
        uint8_t invAlpha = 255 - alpha;
        
        uint8_t r = ((color & 0xFF) * alpha + (bg & 0xFF) * invAlpha) / 255;
        uint8_t g = (((color >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * invAlpha) / 255;
        uint8_t b = (((color >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * invAlpha) / 255;
        
        backbuffer[y * pitch + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
    }
    
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t* GetBackbuffer() { return backbuffer; }
}
