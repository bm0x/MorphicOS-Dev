#include "compositor.h"
#include "graphics.h"
#include "../drm/drm.h"
#include "early_term.h"
#include "alpha_lut.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../../mm/pmm.h"
#include "../../arch/common/mmu.h"
#include "font_renderer.h"
#include "../serial/uart.h"
#include "../../process/scheduler.h"
#include "../input/mouse.h"

namespace Compositor {
    static volatile int lock = 0;
    
    static void AcquireLock() {
        while (__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE)) {
             __asm__ volatile("pause");
        }
    }
    
    static void ReleaseLock() {
        __atomic_clear(&lock, __ATOMIC_RELEASE);
    }

    static Layer* layers[MAX_LAYERS];
    static uint32_t layerCount = 0;
    
    static DirtyRect dirtyRects[MAX_DIRTY_RECTS];
    static uint32_t dirtyCount = 0;
    
    // Special layers
    static Layer* wallpaperLayer = nullptr;
    static Layer* cursorLayer = nullptr;
    static Layer* overlayLayer = nullptr;
    
    static bool debugOverlayVisible = false;
    static bool userspaceMode = false;
    static uint32_t frameCount = 0;
    
    static void DrawWindowDecoration(Layer* layer);

    void Init() {
        layerCount = 0;
        dirtyCount = 0;
        
        for (int i = 0; i < MAX_LAYERS; i++) layers[i] = nullptr;
        for (int i = 0; i < MAX_DIRTY_RECTS; i++) dirtyRects[i].valid = false;
        
        // Create system layers
        uint32_t w = Graphics::GetWidth();
        uint32_t h = Graphics::GetHeight();
        
        // Wallpaper layer (full screen)
        wallpaperLayer = CreateLayer("wallpaper", LayerType::WALLPAPER, w, h);
        if (wallpaperLayer) {
            wallpaperLayer->z_order = 0;
            // Fill with gradient
            for (uint32_t y = 0; y < h; y++) {
                uint32_t color = 0xFF000000 | ((y * 40 / h) << 16) | ((y * 60 / h) << 8) | (80 + y * 50 / h);
                for (uint32_t x = 0; x < w; x++) {
                    wallpaperLayer->buffer[y * w + x] = color;
                }
            }
        }
        
        // Cursor layer (small, alpha)
        cursorLayer = CreateLayer("cursor", LayerType::CURSOR, 16, 16);
        if (cursorLayer) {
            cursorLayer->z_order = 100;
            cursorLayer->has_alpha = true;
            // Draw arrow cursor
            uint32_t* buf = cursorLayer->buffer;
            for (int i = 0; i < 256; i++) buf[i] = 0; // Transparent
            // Simple arrow
            for (int y = 0; y < 12; y++) {
                for (int x = 0; x <= y && x < 8; x++) {
                    buf[y * 16 + x] = 0xFFFFFFFF;
                }
            }
        }
        
        // Debug overlay layer
        overlayLayer = CreateLayer("debug", LayerType::OVERLAY, w, 100);
        if (overlayLayer) {
            overlayLayer->z_order = 200;
            overlayLayer->has_alpha = true;
            overlayLayer->visible = false;
            // Semi-transparent black background
            for (uint32_t i = 0; i < w * 100; i++) {
                overlayLayer->buffer[i] = 0x80000000;
            }
        }
        
        EarlyTerm::Print("[Compositor] Initialized with ");
        EarlyTerm::PrintDec(layerCount);
        EarlyTerm::Print(" layers.\n");
    }
    
    Layer* CreateLayer(const char* name, LayerType type, uint32_t w, uint32_t h) {
        if (layerCount >= MAX_LAYERS) return nullptr;
        
        Layer* layer = (Layer*)kmalloc(sizeof(Layer));
        if (!layer) return nullptr;
        
        kmemset(layer, 0, sizeof(Layer));
        
        // Copy name
        int i = 0;
        while (name[i] && i < 15) { layer->name[i] = name[i]; i++; }
        layer->name[i] = 0;
        
        layer->type = type;
        layer->z_order = (uint16_t)type * 10;
        layer->x = 0;
        layer->y = 0;
        layer->width = w;
        layer->height = h;
        layer->visible = true;
        layer->dirty = true;
        layer->has_alpha = false;
        layer->next = nullptr;
        
        // Allocate buffer
        layer->buffer = (uint32_t*)kmalloc(w * h * sizeof(uint32_t));
        if (!layer->buffer) {
            kfree(layer);
            return nullptr;
        }
        kmemset(layer->buffer, 0, w * h * sizeof(uint32_t));
        
        layers[layerCount++] = layer;
        return layer;
    }
    
    void DestroyLayer(Layer* layer) {
        if (!layer) return;
        if (layer->buffer) kfree(layer->buffer);
        
        // Remove from array
        for (uint32_t i = 0; i < layerCount; i++) {
            if (layers[i] == layer) {
                for (uint32_t j = i; j < layerCount - 1; j++) {
                    layers[j] = layers[j + 1];
                }
                layerCount--;
                break;
            }
        }
        kfree(layer);
    }
    
    void SetLayerPosition(Layer* layer, int32_t x, int32_t y) {
        if (!layer) return;
        MarkDirty(layer->x, layer->y, layer->width, layer->height);
        layer->x = x;
        layer->y = y;
        layer->dirty = true;
    }
    
    void SetLayerVisible(Layer* layer, bool visible) {
        if (!layer) return;
        layer->visible = visible;
        layer->dirty = true;
    }
    
    void SetLayerZOrder(Layer* layer, uint16_t z) {
        if (!layer) return;
        layer->z_order = z;
    }
    
    Layer* GetWallpaperLayer() { return wallpaperLayer; }
    Layer* GetCursorLayer() { return cursorLayer; }
    Layer* GetOverlayLayer() { return overlayLayer; }
    
    void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        // P2: Intelligent Dirty Rect Merging
        for (uint32_t i = 0; i < dirtyCount; i++) {
             DirtyRect* rect = &dirtyRects[i];
             if (!rect->valid) continue;
             
             // Check intersection or proximity (simple AABB)
             bool intersects = !(x > rect->x + rect->w || 
                                 x + w < rect->x || 
                                 y > rect->y + rect->h || 
                                 y + h < rect->y);
                                 
             if (intersects) {
                 // Union rects
                 uint32_t minX = (x < rect->x) ? x : rect->x;
                 uint32_t minY = (y < rect->y) ? y : rect->y;
                 uint32_t maxX = (x + w > rect->x + rect->w) ? x + w : rect->x + rect->w;
                 uint32_t maxY = (y + h > rect->y + rect->h) ? y + h : rect->y + rect->h;
                 
                 rect->x = minX;
                 rect->y = minY;
                 rect->w = maxX - minX;
                 rect->h = maxY - minY;
                 return; // Merged
             }
        }

        if (dirtyCount >= MAX_DIRTY_RECTS) {
            // Full screen dirty
            dirtyRects[0] = {0, 0, Graphics::GetWidth(), Graphics::GetHeight(), true};
            dirtyCount = 1;
            return;
        }
        dirtyRects[dirtyCount++] = {x, y, w, h, true};
    }
    
    void MarkLayerDirty(Layer* layer) {
        if (!layer) return;
        MarkDirty(layer->x, layer->y, layer->width, layer->height);
    }
    
    void MarkLayerReady(void* buffer) {
        // Find layer owning this buffer
        for (uint32_t i = 0; i < layerCount; i++) {
            if (layers[i] && layers[i]->buffer == buffer) {
                layers[i]->frame_ready = true;
                return;
            }
        }
    }
    
    void ClearDirtyRects() {
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirtyRects[i].valid = false;
        }
        dirtyCount = 0;
    }
    
    // Forward declare SIMD blit for opaque spans
    extern "C" void blit_fast_32(void* dest, void* src, size_t count);
    
    void BlitTransparent(uint32_t* dest, uint32_t dest_pitch,
                         uint32_t* src, uint32_t src_w, uint32_t src_h,
                         int32_t dx, int32_t dy) {
        
        int32_t screenW = dest_pitch;
        int32_t screenH = Graphics::GetHeight(); // Assuming global height access or passed in pitch
        // Actually pitch is width usually
        
        // Clipping
        int32_t renderW = (int32_t)src_w;
        int32_t renderH = (int32_t)src_h;
        int32_t srcX = 0;
        int32_t srcY = 0;
        
        // Left
        if (dx < 0) {
            renderW += dx;
            srcX = -dx;
            dx = 0;
        }
        // Top
        if (dy < 0) {
            renderH += dy;
            srcY = -dy;
            dy = 0;
        }
        // Right
        if (dx + renderW > screenW) {
            renderW = screenW - dx;
        }
        // Bottom
        if (dy + renderH > screenH) {
            renderH = screenH - dy;
        }
        
        if (renderW <= 0 || renderH <= 0) return;
        
        // Render Loop - Optimized with SIMD for opaque spans
        for (int32_t y = 0; y < renderH; y++) {
            uint32_t* dstRow = &dest[(dy + y) * dest_pitch + dx];
            uint32_t* srcRow = &src[(srcY + y) * src_w + srcX];
            
            int32_t x = 0;
            while (x < renderW) {
                uint32_t pixel = srcRow[x];
                uint8_t alpha = (pixel >> 24) & 0xFF;
                
                if (alpha == 0) {
                    // Skip transparent pixels
                    x++;
                    continue;
                }
                
                if (alpha == 255) {
                    // OPTIMIZATION: Find consecutive fully opaque pixels and SIMD copy
                    int32_t opaqueStart = x;
                    while (x < renderW && ((srcRow[x] >> 24) & 0xFF) == 255) {
                        x++;
                    }
                    int32_t opaqueCount = x - opaqueStart;
                    
                    // Use SIMD for spans of 4+ pixels, otherwise manual copy
                    if (opaqueCount >= 4) {
                        blit_fast_32(&dstRow[opaqueStart], &srcRow[opaqueStart], opaqueCount);
                    } else {
                        for (int32_t i = 0; i < opaqueCount; i++) {
                            dstRow[opaqueStart + i] = srcRow[opaqueStart + i];
                        }
                    }
                } else {
                    // Partial alpha - use LUT blend
                    dstRow[x] = Alpha::Blend(pixel, dstRow[x]);
                    x++;
                }
            }
        }
    }
    
    // Declare SIMD blit functions (64-bit optimized)
    extern "C" void blit_fast_64(void* dest, void* src, size_t qwords);

    // Sort layers by z_order (Insertion Sort - O(n) for mostly sorted)
    static void SortLayers() {
        for (uint32_t i = 1; i < layerCount; i++) {
            Layer* key = layers[i];
            int j = i - 1;
            while (j >= 0 && layers[j]->z_order > key->z_order) {
                layers[j + 1] = layers[j];
                j = j - 1;
            }
            layers[j + 1] = key;
        }
    }
    
    // Declare SIMD blit locally if not in header
    extern "C" void blit_fast_32(void* dest, void* src, size_t count);

    void Compose() {
        AcquireLock();
        uint32_t* backbuf = Graphics::GetDrawBuffer();
        uint32_t pitch = Graphics::GetWidth();
        
        // NOTE: In userspaceMode, Desktop handles its own rendering.
        // However, we still need to composite APP_WINDOW layers created via syscalls
        // so that spawned apps (Calculator, Terminal) are visible.
        // We skip WALLPAPER/OVERLAY layers which Desktop manages.

        SortLayers();
        
        // Render layers bottom to top
        for (uint32_t i = 0; i < layerCount; i++) {
            Layer* layer = layers[i];
            if (!layer || !layer->visible || !layer->buffer) continue;
            
            // In userspaceMode, only render APP_WINDOW layers (syscall-created)
            if (userspaceMode && layer->type != LayerType::APP_WINDOW) continue;
            
            if (layer->has_alpha) {
                BlitTransparent(backbuf, pitch, layer->buffer, 
                               layer->width, layer->height, layer->x, layer->y);
            } else {
                // Opaque blit - Optimized with blit_fast_32
                // Calculate valid width to avoid out of bounds
                uint32_t safeWidth = layer->width;
                if (layer->x + safeWidth > Graphics::GetWidth()) safeWidth = Graphics::GetWidth() - layer->x;
                
                for (uint32_t y = 0; y < layer->height && (layer->y + y) < Graphics::GetHeight(); y++) {
                    blit_fast_32(&backbuf[(layer->y + y) * pitch + layer->x], 
                                 &layer->buffer[y * layer->width], 
                                 safeWidth);
                }
            }
            
            // Draw Decorations ON TOP of the window layer (or around it)
            if (layer->type == LayerType::APP_WINDOW) {
                DrawWindowDecoration(layer);
            }
        }
        
        frameCount++;
        ClearDirtyRects();
        ReleaseLock();
    }
    
    // Overlay only APP_WINDOW layers onto backbuffer AND draw Cursor on top
    // Called by Desktop after it draws its scene, to integrate spawned app windows
    void ComposeAppWindowsOnly(int mouseX, int mouseY) {
        AcquireLock();
        
        uint32_t* backbuf = Graphics::GetDrawBuffer();
        if (!backbuf) {
            ReleaseLock();
            return;
        }
        
        uint32_t pitch = Graphics::GetWidth();
        // ... (existing locals)
        uint32_t screenH = Graphics::GetHeight();
        
        // Sort layers by z_order
        SortLayers();
        
        // Count APP_WINDOW layers for diagnostics (one-time log)
        static bool logged = false;
        int appWindowCount = 0;
        
        // Only render APP_WINDOW layers (created by spawned apps via syscalls)
        for (uint32_t i = 0; i < layerCount; i++) {
            Layer* layer = layers[i];
            if (!layer) continue;
            if (layer->type != LayerType::APP_WINDOW) continue;
            
            appWindowCount++;
            
            // Log once when we first detect an APP_WINDOW
            if (!logged) {
                UART::Write("[ComposeAppWindowsOnly] Found APP_WINDOW: visible=");
                UART::WriteDec(layer->visible ? 1 : 0);
                UART::Write(" buffer=");
                UART::WriteHex((uint64_t)layer->buffer);
                UART::Write(" pos=");
                UART::WriteDec(layer->x);
                UART::Write(",");
                UART::WriteDec(layer->y);
                UART::Write(" size=");
                UART::WriteDec(layer->width);
                UART::Write("x");
                UART::WriteDec(layer->height);
                // Debug: Check first pixel value
                if (layer->buffer) {
                    UART::Write(" px0=");
                    UART::WriteHex(layer->buffer[0]);
                }
                UART::Write("\n");
                logged = true;
            }
            
            if (!layer->visible) continue;
            // Only draw if the app has signaled that the frame is ready (prevents black/garbage initial frame)
            if (!layer->frame_ready) continue;
            // if (!layer->buffer) continue; // Checked above

            // PERFORMANCE: Mark layer region as dirty for optimized flip
            if (layer->x >= 0 && layer->y >= 0) {
                Graphics::MarkDirty(layer->x, layer->y, layer->width, layer->height);
            }

            // if (layer->has_alpha) ...
            
            if (layer->has_alpha) {
                // Alpha blit needs coordinate check too
                // Skip if entirely off-screen
                if (layer->x + (int32_t)layer->width <= 0 || layer->x >= (int32_t)pitch) continue;
                if (layer->y + (int32_t)layer->height <= 0 || layer->y >= (int32_t)screenH) continue;
                
                // BlitTransparent needs to be updated for signed coords - for now skip off-screen
                if (layer->x >= 0 && layer->y >= 0) {
                    BlitTransparent(backbuf, pitch, layer->buffer, 
                                   layer->width, layer->height, layer->x, layer->y);
                }
            } else {
                // Opaque blit with signed coordinate support
                // Calculate visible region
                int32_t lx = layer->x;
                int32_t ly = layer->y;
                int32_t lw = (int32_t)layer->width;
                int32_t lh = (int32_t)layer->height;
                
                // Skip if entirely off-screen
                if (lx + lw <= 0 || lx >= (int32_t)pitch) continue;
                if (ly + lh <= 0 || ly >= (int32_t)screenH) continue;
                
                // Calculate source and dest offsets
                int32_t srcOffsetX = 0, srcOffsetY = 0;
                int32_t dstX = lx, dstY = ly;
                int32_t copyW = lw, copyH = lh;
                
                // Clip left edge
                if (lx < 0) {
                    srcOffsetX = -lx;
                    dstX = 0;
                    copyW += lx;
                }
                // Clip top edge
                if (ly < 0) {
                    srcOffsetY = -ly;
                    dstY = 0;
                    copyH += ly;
                }
                // Clip right edge
                if (dstX + copyW > (int32_t)pitch) {
                    copyW = (int32_t)pitch - dstX;
                }
                // Clip bottom edge
                if (dstY + copyH > (int32_t)screenH) {
                    copyH = (int32_t)screenH - dstY;
                }
                
                if (copyW <= 0 || copyH <= 0) continue;
                
                // Blit visible portion - SIMD Optimized (64-bit when aligned)
                for (int32_t y = 0; y < copyH; y++) {
                    uint32_t* dst = &backbuf[(dstY + y) * pitch + dstX];
                    uint32_t* src = &layer->buffer[(srcOffsetY + y) * layer->width + srcOffsetX];
                    
                    // Use 64-bit blit if width is even and pointers are 8-byte aligned
                    if ((copyW & 1) == 0 && ((uintptr_t)dst & 7) == 0 && ((uintptr_t)src & 7) == 0) {
                        blit_fast_64(dst, src, copyW / 2);  // 64-bit = 2 pixels per op
                    } else {
                        blit_fast_32(dst, src, copyW);      // 32-bit fallback
                    }
                }
            }
            
            // Draw window decorations (Title bar, buttons)
            DrawWindowDecoration(layer);
        }

        // Cursor Drawing (Kernel Overlay)
        // Fixed: Ensure cursor is drawn ON TOP of apps on the SHARED buffer.
        // Userspace manages background, but Kernel manages Apps + Cursor layering.
        Mouse::DrawCursor(backbuf, Graphics::GetWidth(), Graphics::GetHeight());
        
        // if (cursorLayer) { ... }
        
        ReleaseLock();
    }
    
    void ComposeRegion(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
        uint32_t* backbuf = Graphics::GetDrawBuffer();
        uint32_t pitch = Graphics::GetWidth();
        uint32_t screenH = Graphics::GetHeight();
        
        // Clamp region to screen
        if (rx + rw > pitch) rw = pitch - rx;
        if (ry + rh > screenH) rh = screenH - ry;
        
        if (userspaceMode) return;

        SortLayers();
        
        // Render layers but only within the dirty region
        for (uint32_t i = 0; i < layerCount; i++) {
            Layer* layer = layers[i];
            if (!layer || !layer->visible || !layer->buffer) continue;
            
            // Check if layer intersects dirty region
            if (layer->x >= rx + rw || layer->x + layer->width <= rx) continue;
            if (layer->y >= ry + rh || layer->y + layer->height <= ry) continue;
            
            // Calculate intersection
            uint32_t startX = (layer->x > rx) ? 0 : (rx - layer->x);
            uint32_t startY = (layer->y > ry) ? 0 : (ry - layer->y);
            uint32_t endX = (layer->x + layer->width < rx + rw) ? layer->width : (rx + rw - layer->x);
            uint32_t endY = (layer->y + layer->height < ry + rh) ? layer->height : (ry + rh - layer->y);
            
            for (uint32_t ly = startY; ly < endY; ly++) {
                for (uint32_t lx = startX; lx < endX; lx++) {
                    uint32_t px = layer->x + lx;
                    uint32_t py = layer->y + ly;
                    uint32_t pixel = layer->buffer[ly * layer->width + lx];
                    
                    if (layer->has_alpha) {
                        uint8_t alpha = (pixel >> 24) & 0xFF;
                        if (alpha == 0) continue;
                        if (alpha == 255) {
                            backbuf[py * pitch + px] = pixel;
                        } else {
                            uint32_t bg = backbuf[py * pitch + px];
                            uint8_t invA = 255 - alpha;
                            uint8_t r = ((pixel & 0xFF) * alpha + (bg & 0xFF) * invA) / 255;
                            uint8_t g = (((pixel >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * invA) / 255;
                            uint8_t b = (((pixel >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * invA) / 255;
                            backbuf[py * pitch + px] = 0xFF000000 | (b << 16) | (g << 8) | r;
                        }
                    } else {
                        backbuf[py * pitch + px] = pixel;
                    }
                }
            }
        }
    }
    
    void Flip() {
        // Use DRM for VSync stable presentation
        DRM::Present(true);
    }
    
    void ToggleDebugOverlay() {
        debugOverlayVisible = !debugOverlayVisible;
        if (overlayLayer) {
            overlayLayer->visible = debugOverlayVisible;
        }
    }
    
    bool IsDebugOverlayVisible() {
        return debugOverlayVisible;
    }
    
    void UpdateDebugOverlay() {
        if (!overlayLayer || !debugOverlayVisible) return;
        
        uint32_t w = overlayLayer->width;
        uint32_t h = overlayLayer->height;
        uint32_t* buf = overlayLayer->buffer;
        
        // Clear with semi-transparent dark background
        for (uint32_t i = 0; i < w * h; i++) {
            buf[i] = 0xC0101020;  // 75% opaque dark blue
        }
        
        // Draw border
        for (uint32_t x = 0; x < w; x++) {
            buf[x] = 0xFF00FFFF;  // Cyan top border
            buf[(h-1) * w + x] = 0xFF00FFFF;
        }
        
        // Draw text info (simple colored rectangles for now)
        // F12 indicator - green box
        for (uint32_t y = 10; y < 25; y++) {
            for (uint32_t x = 10; x < 80; x++) {
                buf[y * w + x] = 0xFF00FF00;
            }
        }
        
        // Memory indicator - yellow box
        for (uint32_t y = 35; y < 50; y++) {
            for (uint32_t x = 10; x < 120; x++) {
                buf[y * w + x] = 0xFFFFFF00;
            }
        }
        
        // Frame indicator - cyan box
        for (uint32_t y = 60; y < 75; y++) {
            for (uint32_t x = 10; x < 100; x++) {
                buf[y * w + x] = 0xFF00FFFF;
            }
        }
        
        // Display frame count as colored bar
        uint32_t barLen = (frameCount % 200);
        for (uint32_t y = 80; y < 90; y++) {
            for (uint32_t x = 10; x < 10 + barLen && x < w - 10; x++) {
                buf[y * w + x] = 0xFFFF00FF;  // Magenta progress bar
            }
        }
    }

    // Window System
    // Window System State
    static Window windows[16];
    static uint32_t windowCount = 0;
    static uint64_t nextWindowInfoId = 1;

    // Dragging State
    static Window* dragWindow = nullptr;
    static int32_t dragOffsetX = 0;
    static int32_t dragOffsetY = 0;

    // Helper: Draw Window Decorations (Professional Style)
    static void DrawWindowDecoration(Layer* layer) {
        if (!layer) return;

        uint32_t border = BORDER_WIDTH;
        uint32_t titleH = TITLE_BAR_HEIGHT;
        
        uint32_t lx = layer->x;
        uint32_t ly = layer->y;
        uint32_t lw = layer->width;
        uint32_t lh = layer->height;

        // Professional Palette
        uint32_t c_title_bg   = 0xFF121212;
        uint32_t c_border     = 0xFF2A2A2A;
        uint32_t c_accent     = 0xFF4080A0; // Morphic Blue
        uint32_t c_btn_bg     = 0xFF1A1A1A;
        uint32_t c_btn_border = 0xFF2A2A2A;
        uint32_t c_close      = 0xFFB04040;
        uint32_t c_max        = 0xFF4040B0;
        uint32_t c_min        = 0xFFEAEAEA; // Indicator color

        if (ly < titleH) return; // Prevent out of bounds

        // 1. Title Bar Background
        Graphics::FillRect(lx - border, ly - titleH, lw + 2 * border, titleH, c_title_bg);
        
        // 2. Borders (1px)
        Graphics::FillRect(lx - border, ly, border, lh, c_border); // Left
        Graphics::FillRect(lx + lw, ly, border, lh, c_border);     // Right
        Graphics::FillRect(lx - border, ly + lh, lw + 2 * border, border, c_border); // Bottom
        
        // 3. Accent Line (Bottom of Title Bar)
        Graphics::FillRect(lx, ly - 2, lw, 1, c_accent);

        // 4. Title Text
        FontRenderer::DrawText(Graphics::GetDrawBuffer(), Graphics::GetWidth(), Graphics::GetHeight(),
                               lx + 8, ly - titleH + 6, layer->name, 0xFFE0E0E0, 0);

        // 5. Window Control Buttons
        int btnSize = 14;
        int pad = 6;
        int bx = lx + lw - pad - btnSize;
        int by = ly - titleH + (titleH - btnSize) / 2;

        // Close Button [X]
        Graphics::FillRect(bx, by, btnSize, btnSize, c_btn_bg);
        // DrawRect manually for border
        Graphics::FillRect(bx, by, btnSize, 1, c_btn_border);
        Graphics::FillRect(bx, by+btnSize-1, btnSize, 1, c_btn_border);
        Graphics::FillRect(bx, by, 1, btnSize, c_btn_border);
        Graphics::FillRect(bx+btnSize-1, by, 1, btnSize, c_btn_border);
        // Inner Red
        Graphics::FillRect(bx + 4, by + 4, btnSize - 8, btnSize - 8, c_close);

        // Max Point (Visual)
        bx -= (btnSize + 6);
        Graphics::FillRect(bx, by, btnSize, btnSize, c_btn_bg);
        Graphics::FillRect(bx, by, btnSize, 1, c_btn_border); // Border...
        Graphics::FillRect(bx, by+btnSize-1, btnSize, 1, c_btn_border);
        Graphics::FillRect(bx, by, 1, btnSize, c_btn_border);
        Graphics::FillRect(bx+btnSize-1, by, 1, btnSize, c_btn_border);
        Graphics::FillRect(bx + 4, by + 4, btnSize - 8, btnSize - 8, c_max);

        // Min Point (Visual)
        bx -= (btnSize + 6);
        Graphics::FillRect(bx, by, btnSize, btnSize, c_btn_bg); 
        // Border...
        Graphics::FillRect(bx, by, btnSize, 1, c_btn_border);
        Graphics::FillRect(bx, by+btnSize-1, btnSize, 1, c_btn_border);
        Graphics::FillRect(bx, by, 1, btnSize, c_btn_border);
        Graphics::FillRect(bx+btnSize-1, by, 1, btnSize, c_btn_border);
        // Line
        Graphics::FillRect(bx + 4, by + btnSize - 6, btnSize - 8, 2, c_min);
    }

    Window* CreateWindow(uint32_t w, uint32_t h, uint32_t flags) {
        AcquireLock();
        if (windowCount >= 16) { ReleaseLock(); return nullptr; }
        
        // 1. Allocate Physical Memory (Pages)
        // PMM is thread-safe (usually uses its own lock, verify?)
        uint64_t size = (uint64_t)w * h * 4;
        uint64_t pages = (size + 4095) / 4096;
        
        void* phys_ptr = PMM::AllocContiguous(pages);
        if (!phys_ptr) {
            ReleaseLock();
            EarlyTerm::Print("[Compositor] CreateWindow OOM\n");
            return nullptr;
        }
        
        // 2. Clear Memory
        kmemset(phys_ptr, 0, size); 
        
        // 3. Create Layer manually
        if (layerCount >= MAX_LAYERS) { ReleaseLock(); return nullptr; }
        
        Layer* layer = (Layer*)kmalloc(sizeof(Layer));
        if (!layer) { ReleaseLock(); return nullptr; }
        kmemset(layer, 0, sizeof(Layer));
        
        // Name
        const char* defaultName = "Window";
        for(int i=0; i<6; i++) layer->name[i] = defaultName[i];
        
        layer->type = LayerType::APP_WINDOW;
        layer->width = w;
        layer->height = h;
        layer->buffer = (uint32_t*)phys_ptr; 
        
        layer->visible = true;
        layer->dirty = true;
        layer->has_alpha = false; // Default opaque
        
        uint32_t screenW = Graphics::GetWidth();
        uint32_t screenH = Graphics::GetHeight();
        layer->x = (screenW > w) ? (screenW - w) / 2 : 0;
        layer->y = (screenH > h) ? (screenH - h) / 2 : 0;
        layer->z_order = 50 + windowCount; // Stagger Z-order
        
        layers[layerCount++] = layer;
        
        // 4. Register Window
        Window* win = nullptr;
        for (int i = 0; i < 16; i++) {
             if (windows[i].id == 0) {
                 win = &windows[i];
                 break;
             }
        }
        
        if (win) {
            win->id = nextWindowInfoId++;
            win->layer = layer;
            win->width = w;
            win->height = h;
            win->phys_addr = (uint64_t)phys_ptr;
            win->buffer = phys_ptr;
            win->owner_pid = Scheduler::GetCurrentTaskId();
            windowCount++;
            ReleaseLock();
            return win;
        }
        
        // Cleanup if no window slot
        // TODO: Free layer and phys
        ReleaseLock();
        return nullptr;
    }
    
    void UpdateWindow(uint64_t window_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t flags) {
        Window* win = GetWindow(window_id);
        if (win && win->layer) {
            // Convert to signed coordinates (handle negative via MSB interpretation)
            int32_t sx = (int32_t)x;
            int32_t sy = (int32_t)y;
            
            // Update Position (signed coordinates allow off-screen placement)
            SetLayerPosition(win->layer, sx, sy);
            
            // Handle Flags
            // Flag 0 (bit 0 off) = Hidden/Minimized
            if ((flags & 1) == 0) {
                win->layer->visible = false;
            } else {
                win->layer->visible = true;
            }

            // TODO: Resize not fully implemented yet
            
            MarkDirty(sx - (int32_t)BORDER_WIDTH, sy - (int32_t)TITLE_BAR_HEIGHT, 
                      win->width + 2*BORDER_WIDTH, win->height + TITLE_BAR_HEIGHT + BORDER_WIDTH);
            win->layer->dirty = true;
        }
    }

    // Must match userspace definition
    struct WindowInfo {
        uint64_t id;
        uint32_t x, y, w, h;
        uint32_t flags;
        char title[32];
        uint64_t pid;
    };

    uint32_t GetWindowList(void* user_buf, uint32_t max_count) {
        if (!user_buf) return 0;
        WindowInfo* infos = (WindowInfo*)user_buf;
        uint32_t count = 0;
        
        for (int i = 0; i < 16 && count < max_count; i++) {
            if (windows[i].id != 0 && windows[i].layer) {
                infos[count].id = windows[i].id;
                infos[count].x = windows[i].layer->x;
                infos[count].y = windows[i].layer->y;
                infos[count].w = windows[i].width;
                infos[count].h = windows[i].height;
                infos[count].flags = (windows[i].layer->visible ? 1 : 0);
                
                // Copy title
                const char* name = windows[i].layer->name;
                int j = 0;
                while (j < 31 && name[j]) {
                    infos[count].title[j] = name[j];
                    j++;
                }
                infos[count].title[j] = 0;
                
                infos[count].pid = windows[i].owner_pid;
                
                count++;
            }
        }
        return count;
    }
    
    void DestroyWindow(uint64_t window_id) {
        Window* win = GetWindow(window_id);
        if (win) {
             // TODO: Layer destruction requires care not to free phys buffer with kfree
             // For now we leak slightly to avoid crash or need to refactor DestroyLayer
             // DestroyLayer(win->layer); 
             win->layer->visible = false; // Hide it at least
             win->layer = nullptr;
             
             win->id = 0;
             windowCount--;
        }
    }
    
    Window* GetWindow(uint64_t window_id) {
        for (int i = 0; i < 16; i++) {
            if (windows[i].id == window_id) return &windows[i];
        }
        return nullptr;
    }

    bool ProcessMouseEvent(int32_t x, int32_t y, uint8_t buttons) {
        if (userspaceMode) return false; // Hand off completely to userspace

        static bool wasLeftDown = false;
        bool leftDown = (buttons & 1); // Bit 0 is left button
        bool handled = false;

        // 1. Handle Dragging
        if (dragWindow) {
            handled = true;
            if (leftDown) {
                // Determine new position
                int32_t newX = x - dragOffsetX;
                int32_t newY = y - dragOffsetY;
                
                SetLayerPosition(dragWindow->layer, newX, newY);
                MarkDirty(newX - BORDER_WIDTH, newY - TITLE_BAR_HEIGHT, 
                          dragWindow->width + BORDER_WIDTH*2, dragWindow->height + TITLE_BAR_HEIGHT + BORDER_WIDTH);

            } else {
                // Release Drop
                dragWindow = nullptr;
            }
            // Always consume events during drag
            return true;
        }

        // 2. Handle Click (Start Drag or Buttons)
        if (leftDown && !wasLeftDown) {
            // Sort to ensure we hit the top-most window
            SortLayers(); 
            
            for (int i = layerCount - 1; i >= 0; i--) {
                Layer* layer = layers[i];
                if (!layer || !layer->visible || layer->type != LayerType::APP_WINDOW) continue;

                // Hit Test: Title Bar + Borders
                int32_t titleX = layer->x - BORDER_WIDTH;
                int32_t titleY = layer->y - TITLE_BAR_HEIGHT;
                int32_t titleW = layer->width + 2 * BORDER_WIDTH;
                int32_t titleH = TITLE_BAR_HEIGHT;

                // Check Hit on Title Bar
                if (x >= titleX && x < titleX + titleW && y >= titleY && y < titleY + titleH) {
                    handled = true;

                    // Locate Window Struct
                    Window* win = nullptr;
                    for(int w=0; w<16; w++) {
                        if (windows[w].layer == layer) { win = &windows[w]; break; }
                    }
                    if (!win) return true; // Orphan layer?

                     // Controls Geometry
                     int btnSize = 14;
                     int pad = 6;
                     // Right-to-Left: Close, Max, Min
                     int bxClose = layer->x + layer->width - pad - btnSize;
                     int bxMax   = bxClose - (btnSize + 6);
                     int bxMin   = bxMax - (btnSize + 6);
                     int by      = layer->y - titleH + (titleH - btnSize) / 2;

                     // HIT: Close
                     if (x >= bxClose && x < bxClose + btnSize && y >= by && y < by + btnSize) {
                         DestroyWindow(win->id);
                         return true;
                     }

                     // HIT: Maximize (Toggle)
                     if (x >= bxMax && x < bxMax + btnSize && y >= by && y < by + btnSize) {
                         // Simple Maximize Logic: Move to 0,0 and resize to Screen
                         // Note: We need to store restore state. For now, simple toggle logic.
                         // TODO: Actual resize requires reallocation of buffer (expensive/complex here).
                         // Alternative: Just move to 0,0 and prevent drag?
                         // For now, let's just center it as a placeholder for "Restore"
                         SetLayerPosition(layer, (Graphics::GetWidth() - layer->width)/2, (Graphics::GetHeight() - layer->height)/2);
                         return true;
                     }

                     // HIT: Minimize (Hide)
                     if (x >= bxMin && x < bxMin + btnSize && y >= by && y < by + btnSize) {
                         // Hide Layer
                         SetLayerVisible(layer, false);
                         // TODO: How to restore? This is the "Not inside desktop" problem.
                         // For now, we allow it to vanish. Relaunching app might create new window.
                         return true;
                     }

                     // HIT: Drag (Title Bar, not buttons)
                     dragWindow = win;
                     dragOffsetX = x - layer->x;
                     dragOffsetY = y - layer->y;
                     SetLayerZOrder(layer, 60); // Bring to front
                     return true;
                }
            }
        }
        
        wasLeftDown = leftDown;
        wasLeftDown = leftDown;
        return handled;
    }

    void EnableUserspaceMode() {
        userspaceMode = true;
        // Optionally clear screen or release layers?
        EarlyTerm::Print("[Compositor] Userspace Mode Active (Kernel Rendering Disabled)\n");
    }
}

