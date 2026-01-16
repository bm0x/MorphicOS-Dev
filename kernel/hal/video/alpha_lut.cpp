// Alpha Blending Look-Up Table Implementation
// Pre-computed alpha blending for O(1) transparency operations

#include "alpha_lut.h"
#include "../../mm/heap.h"

namespace Alpha {
    // Definition of extern variables declared in alpha_lut.h
    uint8_t* lut = nullptr;
    bool initialized = false;
    
    // Initialize the lookup table (call once at startup in Graphics::Init)
    void InitLUT() {
        if (initialized) return;
        
        // Allocate 64KB for LUT (256 * 256)
        lut = (uint8_t*)kmalloc(256 * 256 * sizeof(uint8_t));
        
        if (!lut) return; // Alloc failed
        
        for (int a = 0; a < 256; a++) {
            for (int c = 0; c < 256; c++) {
                // LUT formula: (alpha * channel) / 255 ≈ (alpha * channel) >> 8
                lut[a * 256 + c] = (uint8_t)((a * c) >> 8);
            }
        }
        initialized = true;
    }
}
