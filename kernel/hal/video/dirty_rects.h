#pragma once
// Dirty Rectangles System for Morphic OS Compositor
// Only renders regions that have changed since last frame

#include <stdint.h>

#define MAX_DIRTY_RECTS 32

namespace DirtyRects {
    
    struct Rect {
        int32_t x, y;
        uint32_t w, h;
        bool valid;
    };
    
    // Dirty rectangle pool
    static Rect rects[MAX_DIRTY_RECTS];
    static int count = 0;
    
    // Initialize the system
    inline void Init() {
        count = 0;
        for (int i = 0; i < MAX_DIRTY_RECTS; i++) {
            rects[i].valid = false;
        }
    }
    
    // Mark a rectangular region as dirty (needs redraw)
    inline void Mark(int32_t x, int32_t y, uint32_t w, uint32_t h) {
        if (count >= MAX_DIRTY_RECTS) {
            // Overflow: mark entire screen dirty
            rects[0].x = 0;
            rects[0].y = 0;
            rects[0].w = 0xFFFF;  // Will be clamped to screen
            rects[0].h = 0xFFFF;
            rects[0].valid = true;
            count = 1;
            return;
        }
        
        // Check for merge with existing rect (simple overlap)
        for (int i = 0; i < count; i++) {
            if (!rects[i].valid) continue;
            
            // Simple bounding box merge if overlapping
            int32_t rx = rects[i].x;
            int32_t ry = rects[i].y;
            int32_t rw = rects[i].w;
            int32_t rh = rects[i].h;
            
            if (x < rx + (int32_t)rw && x + (int32_t)w > rx &&
                y < ry + (int32_t)rh && y + (int32_t)h > ry) {
                // Merge
                int32_t nx = (x < rx) ? x : rx;
                int32_t ny = (y < ry) ? y : ry;
                int32_t nx2 = ((x + w) > (rx + rw)) ? (x + w) : (rx + rw);
                int32_t ny2 = ((y + h) > (ry + rh)) ? (y + h) : (ry + rh);
                rects[i].x = nx;
                rects[i].y = ny;
                rects[i].w = nx2 - nx;
                rects[i].h = ny2 - ny;
                return;
            }
        }
        
        // Add new rect
        rects[count].x = x;
        rects[count].y = y;
        rects[count].w = w;
        rects[count].h = h;
        rects[count].valid = true;
        count++;
    }
    
    // Get list of dirty rects for rendering
    inline int GetCount() { return count; }
    inline Rect* GetRect(int index) { return &rects[index]; }
    
    // Clear all dirty rects (after frame is rendered)
    inline void Clear() {
        count = 0;
        for (int i = 0; i < MAX_DIRTY_RECTS; i++) {
            rects[i].valid = false;
        }
    }
    
    // Check if any region is dirty
    inline bool HasDirty() { return count > 0; }
}
