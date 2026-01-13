#pragma once

#include "vfs.h"

// InitRD - Initial RAM Disk Driver
// Provides a simple in-memory filesystem mounted at /

namespace InitRD {
    // Initialize the RAM filesystem from TAR image
    void Init(uint64_t initrdAddr, uint64_t initrdSize);
    
    // Get the InitRD root node
    VFSNode* GetRoot();
}
