#include "graphics.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../serial/uart.h"
#include "early_term.h"
#include "alpha_lut.h"
#include "../../mm/pmm.h"
#include "../../drivers/gpu/bga.h"

// SIMD optimized memory operations (from blit_fast.S)
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
    void memset_fast_32(void* dest, uint32_t value, size_t count);
}

// IO port inline function for V-Sync
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

namespace Graphics {

    static FramebufferInfo* framebuffer = nullptr;
    static uint32_t* backbuffer = nullptr;     // RAM buffer (userspace draws here)
    static uint32_t* vramBuffer = nullptr;     // VRAM buffer (display target)
    static uint32_t width = 0;
    static uint32_t height = 0;
    static uint32_t pitch = 0;
    
    // GPU Driver Instance
    static BGADriver bga;
    static bool gpuEnabled = false;
    
    // === DIRTY RECT TRACKING FOR 60 FPS ===
    // Track up to MAX_DIRTY_RECTS regions that need updating
    static constexpr uint32_t MAX_DIRTY_RECTS = 16;
    
    struct DirtyRect {
        uint32_t x, y, w, h;
        bool active;
    };
    
    static DirtyRect dirtyRects[MAX_DIRTY_RECTS];
    static uint32_t dirtyRectCount = 0;
    static bool fullScreenDirty = true;  // First frame needs full copy
    
    // Merge overlapping/adjacent dirty rects to reduce blit count
    static void MergeDirtyRects() {
        if (dirtyRectCount <= 1) return;
        
        // Simple merge: combine all into single bounding box if > 4 rects
        // This trades precision for fewer iterations
        if (dirtyRectCount > 4) {
            uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
            for (uint32_t i = 0; i < dirtyRectCount; i++) {
                if (!dirtyRects[i].active) continue;
                if (dirtyRects[i].x < minX) minX = dirtyRects[i].x;
                if (dirtyRects[i].y < minY) minY = dirtyRects[i].y;
                if (dirtyRects[i].x + dirtyRects[i].w > maxX) maxX = dirtyRects[i].x + dirtyRects[i].w;
                if (dirtyRects[i].y + dirtyRects[i].h > maxY) maxY = dirtyRects[i].y + dirtyRects[i].h;
            }
            dirtyRects[0] = {minX, minY, maxX - minX, maxY - minY, true};
            dirtyRectCount = 1;
        }
    }
    
    void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        // Clamp to screen bounds
        if (x >= width || y >= height) return;
        if (x + w > width) w = width - x;
        if (y + h > height) h = height - y;
        
        // If already full screen dirty, skip
        if (fullScreenDirty) return;
        
        // Check if this rect covers most of screen - just mark full dirty
        if (w * h > (width * height * 3 / 4)) {
            fullScreenDirty = true;
            dirtyRectCount = 0;
            return;
        }
        
        // Find empty slot or expand existing overlapping rect
        for (uint32_t i = 0; i < dirtyRectCount; i++) {
            DirtyRect& r = dirtyRects[i];
            if (!r.active) continue;
            
            // Check for overlap - expand if overlapping
            bool overlapsX = !(x + w < r.x || x > r.x + r.w);
            bool overlapsY = !(y + h < r.y || y > r.y + r.h);
            
            if (overlapsX && overlapsY) {
                // Merge into existing rect
                uint32_t newX = (x < r.x) ? x : r.x;
                uint32_t newY = (y < r.y) ? y : r.y;
                uint32_t newMaxX = ((x + w) > (r.x + r.w)) ? (x + w) : (r.x + r.w);
                uint32_t newMaxY = ((y + h) > (r.y + r.h)) ? (y + h) : (r.y + r.h);
                r.x = newX;
                r.y = newY;
                r.w = newMaxX - newX;
                r.h = newMaxY - newY;
                return;
            }
        }
        
        // Add new rect if space available
        if (dirtyRectCount < MAX_DIRTY_RECTS) {
            dirtyRects[dirtyRectCount++] = {x, y, w, h, true};
        } else {
            // No space - fall back to full screen
            fullScreenDirty = true;
        }
    }
    
    void ClearDirtyRects() {
        dirtyRectCount = 0;
        fullScreenDirty = false;
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirtyRects[i].active = false;
        }
    }
    
    bool HasDirtyRects() {
        return fullScreenDirty || dirtyRectCount > 0;
    }
    
    void FlipDirty() {
        if (!framebuffer || !backbuffer) return;
        if (backbuffer == vramBuffer) return;
        
        // VSync Wait (Hardware) - only if GPU enabled
        if (gpuEnabled) {
            bga.WaitVSync();
        }
        
        if (fullScreenDirty) {
            // Full screen copy
            blit_fast_32(vramBuffer, backbuffer, width * height);
        } else if (dirtyRectCount > 0) {
            // Merge rects for efficiency
            MergeDirtyRects();
            
            // Copy only dirty regions
            for (uint32_t i = 0; i < dirtyRectCount; i++) {
                if (!dirtyRects[i].active) continue;
                
                DirtyRect& r = dirtyRects[i];
                for (uint32_t row = 0; row < r.h; row++) {
                    uint32_t offset = (r.y + row) * pitch + r.x;
                    blit_fast_32(&vramBuffer[offset], &backbuffer[offset], r.w);
                }
            }
        }
        // else: nothing dirty, no copy needed!
        
        // Clear dirty state for next frame
        ClearDirtyRects();
    }
    
    void Init(FramebufferInfo* fb) {
        UART::Write("[Graphics::Init] START\n");
        
        // Initialize VSync HAL (detects if hardware VSync is available)
        VSync::Init();
        
        framebuffer = fb;
        width = fb->width;
        height = fb->height;
        pitch = fb->pixelsPerScanLine;
        
        UART::Write("[Graphics::Init] fb assigned, w=");
        UART::WriteDec(width);
        UART::Write(" h=");
        UART::WriteDec(height);
        UART::Write(" pitch=");
        UART::WriteDec(pitch);
        UART::Write("\n");
        
        // Initialize Alpha LUT for fast blending
        UART::Write("[Graphics::Init] About to call Alpha::InitLUT\n");
        Alpha::InitLUT();
        UART::Write("[Graphics::Init] Alpha::InitLUT done\n");
        
        // Calculate buffer requirements
        uint32_t bufferSize = width * height * sizeof(uint32_t);
        uint32_t pages = (bufferSize + 4095) / 4096;
        
        // 1. Try Initialize GPU (BGA)
        UART::Write("[Graphics] Attempting BGA Init...\n");
        bool bgaAcc = false;
        
        if (bga.Init()) {
            bga.SetMode(width, height, 32);
            // Default: Display Buffer 0
            
            // Map BGA LFB as vramBuffer
            // We need to know where BGA LFB is. Usually 0xE0000000 or similar.
            // But bga.Init() doesn't return it? 
            // We can assume fb->baseAddress *might* be valid if GOP set it up?
            // Safer: Use bga internal pointer if available, or just trust standard LFB.
            // For now, let's trust that bootloader gave us a valid LFB in fb->baseAddress 
            // that corresponds to what BGA controls.
            
            bgaAcc = true;
            gpuEnabled = true;
            UART::Write("[Graphics] BGA Initialized. Hardware Acceleration Ready.\n");
        } 
        
        // STANDARD STABLE DOUBLE BUFFERING
        // Always allocate a system RAM backbuffer.
        // This ensures drawing operations never touch VRAM directly (slow & tear-prone).
        
        UART::Write("[Graphics] Allocating System RAM Backbuffer (Stable)... ");
        void* phys = PMM::AllocContiguous(pages);
        
        if (phys) {
            backbuffer = (uint32_t*)phys;
            memset_fast_32(backbuffer, 0, width * height);
            UART::Write("Success. backbuffer virt=");
            UART::WriteHex((uint64_t)backbuffer);
            UART::Write(" phys=");
            UART::WriteHex((uint64_t)phys);
            UART::Write("\n");
        } else {
             UART::Write("FAILED. Critical Error.\n");
             // Emergency fallback
             backbuffer = (uint32_t*)framebuffer->baseAddress;
        }
        
        // VRAM Destination (Frontbuffer)
        vramBuffer = (uint32_t*)framebuffer->baseAddress;

        
        UART::Write("[Graphics::Init] COMPLETE\n");
    }
    
    void Flip() {
        if (!framebuffer && !gpuEnabled) return;
        
        // VSync Wait (Hardware)
        if (gpuEnabled) {
            bga.WaitVSync();
        }
        
        // OPTIMIZED: Use dirty rect tracking if available
        // If nothing has been explicitly marked dirty, assume full screen dirty
        // (for backwards compatibility with code that doesn't use MarkDirty)
        if (!HasDirtyRects()) {
            fullScreenDirty = true;
        }
        
        if (!backbuffer || !vramBuffer) return;
        if (backbuffer == vramBuffer) return;
        
        // Use optimized dirty rect copy
        if (fullScreenDirty) {
            // Full screen copy - SIMD optimized
            blit_fast_32(vramBuffer, backbuffer, width * height);
        } else {
            // Merge rects for efficiency
            MergeDirtyRects();
            
            // Copy only dirty regions - significant bandwidth reduction
            for (uint32_t i = 0; i < dirtyRectCount; i++) {
                if (!dirtyRects[i].active) continue;
                
                DirtyRect& r = dirtyRects[i];
                for (uint32_t row = 0; row < r.h; row++) {
                    uint32_t offset = (r.y + row) * pitch + r.x;
                    blit_fast_32(&vramBuffer[offset], &backbuffer[offset], r.w);
                }
            }
        }
        
        // Clear dirty state for next frame
        ClearDirtyRects();
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
    
    // Pure blending logic (static helper)
    // Pure blending logic (static helper)
    // Formula: ((FG * Alpha) + (BG * (255 - Alpha))) >> 8
    uint32_t BlendPixelRaw(uint32_t bg, uint32_t fg) {
        // Optimized O(1) LUT Blending (P0 Task)
        // Note: Alpha::Blend takes (fg, bg)
        return Alpha::Blend(fg, bg);
    }


    void BlendPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (!backbuffer || x >= width || y >= height) return;
        uint32_t bg = backbuffer[y * pitch + x];
        backbuffer[y * pitch + x] = BlendPixelRaw(bg, color);
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
    uint32_t* GetFramebuffer() { return framebuffer ? (uint32_t*)framebuffer->baseAddress : nullptr; }
    
    // === ATOMIC COMPOSITION ===
    
    // Spinlock for atomic flip operations
    static volatile uint32_t flipLock = 0;
    
    static inline void AcquireFlipLock() {
        while (__atomic_test_and_set(&flipLock, __ATOMIC_ACQUIRE));
    }
    
    static inline void ReleaseFlipLock() {
        __atomic_clear(&flipLock, __ATOMIC_RELEASE);
    }
    

    
    void FlipWithVSync() {
        if (!framebuffer || !backbuffer) return;
        if (backbuffer == (uint32_t*)framebuffer->baseAddress) return;
        
        // Wait for vertical blank (Safe mechanism)
        VSync::WaitForRetrace();
        
        // Acquire lock for atomic flip
        AcquireFlipLock();
        
        // SIMD copy backbuffer → framebuffer
        uint32_t* dest = (uint32_t*)framebuffer->baseAddress;
        blit_fast_32(dest, backbuffer, pitch * height);
        
        ReleaseFlipLock();
    }
    
    void DrawCursorOnFramebuffer(int16_t x, int16_t y, const uint32_t* sprite, uint32_t w, uint32_t h) {
        if (!framebuffer || !sprite) return;
        if (x < 0 || y < 0) return;
        
        uint32_t* fb = (uint32_t*)framebuffer->baseAddress;
        
        // Acquire lock to prevent collision with flip
        AcquireFlipLock();
        
        for (uint32_t row = 0; row < h && (y + row) < height; row++) {
            for (uint32_t col = 0; col < w && (x + col) < width; col++) {
                uint32_t pixel = sprite[row * w + col];
                if (pixel != 0x00000000) {  // Not transparent
                    fb[(y + row) * pitch + (x + col)] = pixel;
                }
            }
        }
        
        ReleaseFlipLock();
    }
}
