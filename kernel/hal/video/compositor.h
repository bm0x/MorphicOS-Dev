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
}
