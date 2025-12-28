#pragma once

#include "vfs.h"

// InitRD - Initial RAM Disk Driver
// Provides a simple in-memory filesystem mounted at /

namespace InitRD {
    // Initialize the RAM filesystem with default files
    void Init();
    
    // Get the InitRD root node
    VFSNode* GetRoot();
}
