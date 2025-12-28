#pragma once
// Render API for Morphic OS
// Provides request_render_rect for partial screen updates

#include <stdint.h>
#include "../hal/video/dirty_rects.h"

namespace RenderAPI {
    
    // Request redraw of a specific rectangular region
    // This is the primary API for Lua/widgets to request updates
    inline void RequestRect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
        DirtyRects::Mark(x, y, w, h);
    }
    
    // Request full screen redraw
    inline void RequestFullRedraw() {
        DirtyRects::Mark(0, 0, 0xFFFF, 0xFFFF);
    }
    
    // For cursor movement: invalidate old and new positions
    inline void InvalidateCursor(int32_t oldX, int32_t oldY, int32_t newX, int32_t newY,
                                 uint32_t cursorW = 16, uint32_t cursorH = 20) {
        DirtyRects::Mark(oldX, oldY, cursorW, cursorH);
        DirtyRects::Mark(newX, newY, cursorW, cursorH);
    }
    
    // For button/widget hover: invalidate the widget area
    inline void InvalidateWidget(int32_t x, int32_t y, uint32_t w, uint32_t h) {
        DirtyRects::Mark(x, y, w, h);
    }
}

// Syscall interface for Lua
extern "C" {
    // Syscall: SYS_RENDER_RECT (id = 0x30)
    inline void sys_request_render_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
        RenderAPI::RequestRect(x, y, w, h);
    }
}
