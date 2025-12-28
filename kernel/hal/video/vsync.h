#pragma once

#include <stdint.h>

// V-Sync for smooth buffer swapping
namespace VSync {
    // Wait for vertical retrace (eliminates tearing)
    void WaitForRetrace();
    
    // Check if currently in vertical retrace
    bool IsInRetrace();
}
