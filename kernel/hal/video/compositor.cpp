#include "compositor.h"
#include "graphics.h"
#include "early_term.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../../mm/pmm.h"
#include "../../mm/pmm.h"
#include "../../arch/common/mmu.h"
#include "font_renderer.h"

namespace Compositor {
    static Layer* layers[MAX_LAYERS];
    static uint32_t layerCount = 0;
    
    static DirtyRect dirtyRects[MAX_DIRTY_RECTS];
    static uint32_t dirtyCount = 0;
    
    // Special layers
    static Layer* wallpaperLayer = nullptr;
    static Layer* cursorLayer = nullptr;
    static Layer* overlayLayer = nullptr;
    
    static bool debugOverlayVisible = false;
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
    
    void SetLayerPosition(Layer* layer, uint32_t x, uint32_t y) {
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
    
    void ClearDirtyRects() {
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirtyRects[i].valid = false;
        }
        dirtyCount = 0;
    }
    
    void BlitTransparent(uint32_t* dest, uint32_t dest_pitch,
                         uint32_t* src, uint32_t src_w, uint32_t src_h,
                         uint32_t dx, uint32_t dy) {
        for (uint32_t y = 0; y < src_h; y++) {
            for (uint32_t x = 0; x < src_w; x++) {
                uint32_t pixel = src[y * src_w + x];
                uint8_t alpha = (pixel >> 24) & 0xFF;
                
                if (alpha == 0) continue;
                
                uint32_t destIdx = (dy + y) * dest_pitch + (dx + x);
                
                if (alpha == 255) {
                    dest[destIdx] = pixel;
                } else {
                    // Alpha blend
                    uint32_t bg = dest[destIdx];
                    uint8_t invA = 255 - alpha;
                    
                    uint8_t r = ((pixel & 0xFF) * alpha + (bg & 0xFF) * invA) / 255;
                    uint8_t g = (((pixel >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * invA) / 255;
                    uint8_t b = (((pixel >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * invA) / 255;
                    
                    dest[destIdx] = 0xFF000000 | (b << 16) | (g << 8) | r;
                }
            }
        }
    }
    
    // Sort layers by z_order
    static void SortLayers() {
        for (uint32_t i = 0; i < layerCount; i++) {
            for (uint32_t j = i + 1; j < layerCount; j++) {
                if (layers[j]->z_order < layers[i]->z_order) {
                    Layer* tmp = layers[i];
                    layers[i] = layers[j];
                    layers[j] = tmp;
                }
            }
        }
    }
    
    void Compose() {
        uint32_t* backbuf = Graphics::GetBackbuffer();
        uint32_t pitch = Graphics::GetWidth();
        
        SortLayers();
        
        // Render layers bottom to top
        for (uint32_t i = 0; i < layerCount; i++) {
            Layer* layer = layers[i];
            if (!layer || !layer->visible || !layer->buffer) continue;
            
            if (layer->has_alpha) {
                BlitTransparent(backbuf, pitch, layer->buffer, 
                               layer->width, layer->height, layer->x, layer->y);
            } else {
                // Opaque blit
                for (uint32_t y = 0; y < layer->height && (layer->y + y) < Graphics::GetHeight(); y++) {
                    for (uint32_t x = 0; x < layer->width && (layer->x + x) < Graphics::GetWidth(); x++) {
                        backbuf[(layer->y + y) * pitch + (layer->x + x)] = 
                            layer->buffer[y * layer->width + x];
                    }
                }
            }
            
            // Draw Decorations ON TOP of the window layer (or around it)
            if (layer->type == LayerType::APP_WINDOW) {
                DrawWindowDecoration(layer);
            }
        }
        
        frameCount++;
        ClearDirtyRects();
    }
    
    void ComposeRegion(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
        uint32_t* backbuf = Graphics::GetBackbuffer();
        uint32_t pitch = Graphics::GetWidth();
        uint32_t screenH = Graphics::GetHeight();
        
        // Clamp region to screen
        if (rx + rw > pitch) rw = pitch - rx;
        if (ry + rh > screenH) rh = screenH - ry;
        
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
        // Use VSync to eliminate tearing
        Graphics::FlipWithVSync();
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
        FontRenderer::DrawText(Graphics::GetBackbuffer(), Graphics::GetWidth(), Graphics::GetHeight(),
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
        if (windowCount >= 16) return nullptr;
        
        // 1. Allocate Physical Memory (Pages)
        uint64_t size = (uint64_t)w * h * 4;
        uint64_t pages = (size + 4095) / 4096;
        
        void* phys_ptr = PMM::AllocContiguous(pages);
        if (!phys_ptr) {
            EarlyTerm::Print("[Compositor] CreateWindow OOM\n");
            return nullptr;
        }
        
        // 2. Clear Memory (Access via Identity Map or assumption)
        // Ideally map it, but for now we assume physical is accessible
        kmemset(phys_ptr, 0, size); 
        
        // 3. Create Layer manually
        if (layerCount >= MAX_LAYERS) return nullptr;
        
        Layer* layer = (Layer*)kmalloc(sizeof(Layer));
        if (!layer) return nullptr;
        kmemset(layer, 0, sizeof(Layer));
        
        // Name
        const char* defaultName = "Window";
        for(int i=0; i<6; i++) layer->name[i] = defaultName[i];
        
        layer->type = LayerType::APP_WINDOW;
        layer->width = w;
        layer->height = h;
        layer->buffer = (uint32_t*)phys_ptr; // Physical Address used as kernel pointer?
        // WARNING: If this is high memory (> 16MB) and not identity mapped, this crashes.
        // However, we rely on bootloader identity map.
        
        layer->visible = true;
        layer->dirty = true;
        layer->has_alpha = false; // Default opaque
        
        uint32_t screenW = Graphics::GetWidth();
        uint32_t screenH = Graphics::GetHeight();
        layer->x = (screenW > w) ? (screenW - w) / 2 : 0;
        layer->y = (screenH > h) ? (screenH - h) / 2 : 0;
        layer->z_order = 50; 
        
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
            windowCount++;
            return win;
        }
        return nullptr;
    }
    
    void UpdateWindow(uint64_t window_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        Window* win = GetWindow(window_id);
        if (win && win->layer) {
            // Convert to Screen Coords
            uint32_t screenX = win->layer->x + x;
            uint32_t screenY = win->layer->y + y;
            
            MarkDirty(screenX, screenY, w, h);
            win->layer->dirty = true;
            
            // Immediate composition
            Compose();
            Flip();
        }
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
        return handled;
    }
}

