#pragma once

#include "layer.h"
#include "graphics.h"

// Compositor - Manages layer stacking and rendering
namespace Compositor {
    // Initialize compositor
    void Init();
    
    // Layer management
    Layer* CreateLayer(const char* name, LayerType type, uint32_t w, uint32_t h);
    void DestroyLayer(Layer* layer);
    void SetLayerPosition(Layer* layer, uint32_t x, uint32_t y);
    void SetLayerVisible(Layer* layer, bool visible);
    void SetLayerZOrder(Layer* layer, uint16_t z);
    
    // Get layers
    Layer* GetWallpaperLayer();
    Layer* GetCursorLayer();
    Layer* GetOverlayLayer();
    
    // Dirty rectangle tracking
    void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void MarkLayerDirty(Layer* layer);
    void ClearDirtyRects();
    
    // Composition
    void Compose();           // Render all layers to backbuffer
    void ComposeAppWindowsOnly(); // Overlay APP_WINDOW layers only (for Desktop)
    void ComposeRegion(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void Flip();              // Swap backbuffer to screen
    
    // Alpha blending blit
    void BlitTransparent(uint32_t* dest, uint32_t dest_pitch,
                         uint32_t* src, uint32_t src_w, uint32_t src_h,
                         uint32_t x, uint32_t y);
    
    // Debug overlay
    void ToggleDebugOverlay();
    bool IsDebugOverlayVisible();
    void UpdateDebugOverlay();
    
    // Input
    // Returns true if the event was consumed by the Compositor (e.g. dragging, buttons)
    bool ProcessMouseEvent(int32_t x, int32_t y, uint8_t buttons);

    // Userspace Composition Control
    void EnableUserspaceMode();

    // Constants (Professional Style)
    const uint32_t TITLE_BAR_HEIGHT = 26;
    const uint32_t BORDER_WIDTH = 1;

    // Window Management (Userspace API Helpers)
    struct Window {
        uint64_t id;
        Layer* layer;
        uint32_t width;
        uint32_t height;
        void* buffer; // Kernel virt address of buffer
        uint64_t phys_addr; // Physical address for mapping
    };
    
    Window* CreateWindow(uint32_t w, uint32_t h, uint32_t flags);
    void UpdateWindow(uint64_t window_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void DestroyWindow(uint64_t window_id);
    Window* GetWindow(uint64_t window_id);
    // Returns array of Window structs + counts. Max 64 windows.
    // user_buf: pointer to buffer in user space (struct WindowInfo)
    uint32_t GetWindowList(void* user_buf, uint32_t max_count);
}
