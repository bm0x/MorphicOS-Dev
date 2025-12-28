#pragma once

#include <stdint.h>

// Buffer Cache - Ultra-fast sector caching for block devices
// Implements simple LRU eviction

namespace BufferCache {
    void Init();
    
    // Try to get a sector from cache
    // Returns true if found, copies data to buffer
    bool Get(const char* device_name, uint64_t lba, void* buffer, uint32_t sector_size);
    
    // Put a sector into cache
    void Put(const char* device_name, uint64_t lba, void* data, uint32_t sector_size);
    
    // Invalidate a specific sector (after write)
    void Invalidate(const char* device_name, uint64_t lba);
    
    // Flush all cache entries for a device
    void FlushDevice(const char* device_name);
    
    // Get cache statistics
    uint32_t GetHitCount();
    uint32_t GetMissCount();
}
