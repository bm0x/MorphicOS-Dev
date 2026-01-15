#pragma once

#include <stdint.h>

// MountManager - Automatic Filesystem Detection and Mounting
// Scans storage devices, detects filesystem types, and mounts them to VFS

namespace MountManager {
    // Initialize the mount manager
    void Init();
    
    // Scan all storage devices and mount detected filesystems
    // Returns number of filesystems mounted
    int ScanAndMount();
    
    // Get mount count
    int GetMountCount();
    
    // Mount point info for syscall
    struct MountInfo {
        char path[32];
        char fstype[16];
        char device[16];
    };
    
    // Get mount info by index
    bool GetMountInfo(int index, MountInfo* info);
}
