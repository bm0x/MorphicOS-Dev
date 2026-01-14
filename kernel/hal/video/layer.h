#pragma once

#include <stdint.h>

// Layer types for Z-ordering
enum class LayerType {
    WALLPAPER = 0,  // Z=0 (bottom)
    WINDOW = 1,     // Z=1 (Generic)
    APP_WINDOW = 2, // Z=50+ (Apps)
    CURSOR = 3,     // Z=100
    OVERLAY = 4     // Z=200 (top)
};

// Dirty rectangle for partial updates
struct DirtyRect {
    uint32_t x, y, w, h;
    bool valid;
};

// Layer structure for compositor
struct Layer {
    char name[16];         // Layer identifier
    LayerType type;
    uint16_t z_order;      // Lower = behind, higher = front
    
    uint32_t x, y;         // Position on screen
    uint32_t width, height;
    uint32_t* buffer;      // BGRA pixel data
    
    bool visible;
    bool dirty;            // Needs redraw
    bool has_alpha;        // Use alpha blending
    
    Layer* next;           // Linked list for layer stack
};

// Maximum layers
#define MAX_LAYERS 16
#define MAX_DIRTY_RECTS 32
