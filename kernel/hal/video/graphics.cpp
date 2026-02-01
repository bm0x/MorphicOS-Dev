#include "graphics.h"
#include "../drm/drm.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../serial/uart.h"
#include "../../drivers/gpu/bga.h"

// SIMD optimized memory operations
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
    void memset_fast_32(void* dest, uint32_t value, size_t count);
}

namespace Graphics {

    static uint32_t* vramBuffer = nullptr;
    static uint32_t* drawBuffer = nullptr; // Current target
    static uint32_t screenWidth = 0;
    static uint32_t screenHeight = 0;
    static uint32_t screenPitch = 0;
    static bool drmInitialized = false;

    void Init(FramebufferInfo* fb) {
        if (!fb) return;
        
        vramBuffer = (uint32_t*)fb->baseAddress;
        screenWidth = fb->width;
        screenHeight = fb->height;
        screenPitch = fb->pixelsPerScanLine * fb->bytesPerPixel;
        
        // Default drawing to VRAM during boot
        drawBuffer = vramBuffer;
        
        UART::Write("[Graphics] Init: ");
        UART::WriteDec(screenWidth);
        UART::Write("x");
        UART::WriteDec(screenHeight);
        UART::Write("\n");
        
        // Initialize DRM subsystem
        DRM::Init(vramBuffer, screenWidth, screenHeight, screenPitch);
        drmInitialized = true;
    }
    
    uint32_t GetWidth() { return screenWidth; }
    uint32_t GetHeight() { return screenHeight; }
    uint32_t GetPitch() { return screenPitch; }
    uint32_t* GetVRAM() { return vramBuffer; }
    
    uint32_t* GetDrawBuffer() {
        return drawBuffer ? drawBuffer : vramBuffer;
    }
    
    void SetDrawBuffer(uint32_t* buffer) {
        drawBuffer = buffer;
    }

    //=========================================================================
    // Primitives
    //=========================================================================
    
    void PutPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!drawBuffer || x >= screenWidth || y >= screenHeight) return;
        drawBuffer[y * screenWidth + x] = color;
    }
    
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        if (!drawBuffer) return;
        
        // Clipping
        if (x >= screenWidth || y >= screenHeight) return;
        if (x + w > screenWidth) w = screenWidth - x;
        if (y + h > screenHeight) h = screenHeight - y;
        
        for (uint32_t iy = 0; iy < h; iy++) {
            uint32_t* row = &drawBuffer[(y + iy) * screenWidth + x];
            memset_fast_32(row, color, w);
        }
    }
    
    void DrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        FillRect(x, y, w, 1, color);
        FillRect(x, y + h - 1, w, 1, color);
        FillRect(x, y, 1, h, color);
        FillRect(x + w - 1, y, 1, h, color);
    }
    
    void DrawImage(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* data) {
        if (!drawBuffer || !data) return;
        
        // Basic clipping
        if (x >= screenWidth || y >= screenHeight) return;
        uint32_t copyW = w;
        uint32_t copyH = h;
        if (x + copyW > screenWidth) copyW = screenWidth - x;
        if (y + copyH > screenHeight) copyH = screenHeight - y;
        
        for (uint32_t iy = 0; iy < copyH; iy++) {
            uint32_t* dst = &drawBuffer[(y + iy) * screenWidth + x];
            uint32_t* src = &data[iy * w];
            blit_fast_32(dst, src, copyW);
        }
    }

    void DrawImageCentered(uint32_t w, uint32_t h, uint32_t* data) {
        uint32_t x = (screenWidth - w) / 2;
        uint32_t y = (screenHeight - h) / 2;
        DrawImage(x, y, w, h, data);
    }
    
    uint32_t BlendPixelRaw(uint32_t bg, uint32_t fg) {
        uint32_t alpha = (fg >> 24) & 0xFF;
        if (alpha == 0) return bg;
        if (alpha == 255) return fg;
        
        uint32_t inv_alpha = 255 - alpha;
        uint32_t r = ((((fg >> 16) & 0xFF) * alpha) + (((bg >> 16) & 0xFF) * inv_alpha)) >> 8;
        uint32_t g = ((((fg >> 8) & 0xFF) * alpha) + (((bg >> 8) & 0xFF) * inv_alpha)) >> 8;
        uint32_t b = (((fg & 0xFF) * alpha) + ((bg & 0xFF) * inv_alpha)) >> 8;
        
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    void BlendPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!drawBuffer || x >= screenWidth || y >= screenHeight) return;
        
        uint32_t bg = drawBuffer[y * screenWidth + x];
        drawBuffer[y * screenWidth + x] = BlendPixelRaw(bg, color);
    }
    
    void BlitBuffer(uint32_t* dest, uint32_t* src, uint32_t count) {
        blit_fast_32(dest, src, count);
    }

    //=========================================================================
    // Compatibility Wrappers (Delegate to DRM)
    //=========================================================================
    
    void Flip() {
        if (drmInitialized) {
            // Full present (async, no vsync wait to avoid blocking kernel unless needed)
            DRM::Present(false); 
        }
    }
    
    void FlipRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (drmInitialized) {
            DRM::MarkDirty(x, y, w, h);
            DRM::Present(false);
        }
    }
    
    void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (drmInitialized) {
            DRM::MarkDirty(x, y, w, h);
        }
    }
    
    void ClearDirtyRects() {
        if (drmInitialized) {
            DRM::ClearDirty();
        }
    }
}
