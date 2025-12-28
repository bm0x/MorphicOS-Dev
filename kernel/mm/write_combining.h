// Write-Combining Memory Optimization
// Uses PAT (Page Attribute Table) to accelerate framebuffer writes

#pragma once

#include <stdint.h>
#include <stddef.h>


namespace WriteCombining {
    // Initialize PAT with Write-Combining in index 1
    void InitPAT();
    
    // Mark a memory range as Write-Combining
    // Returns true if successful
    bool MarkRegion(uint64_t baseAddr, size_t size);
    
    // Check if Write-Combining is available
    bool IsSupported();
}
