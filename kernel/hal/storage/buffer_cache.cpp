#include "buffer_cache.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"

namespace BufferCache {
    // Cache configuration
    static const uint32_t CACHE_SIZE = 64;      // Number of cached sectors
    static const uint32_t MAX_SECTOR_SIZE = 512; // Max sector size to cache
    
    // Cache entry
    struct CacheEntry {
        char device_name[16];
        uint64_t lba;
        uint8_t data[MAX_SECTOR_SIZE];
        uint32_t access_count;   // For LRU
        bool valid;
    };
    
    static CacheEntry* cache = nullptr;
    static uint32_t hitCount = 0;
    static uint32_t missCount = 0;
    static uint32_t accessCounter = 0;
    
    void Init() {
        cache = (CacheEntry*)kmalloc(sizeof(CacheEntry) * CACHE_SIZE);
        if (cache) {
            kmemset(cache, 0, sizeof(CacheEntry) * CACHE_SIZE);
        }
        hitCount = 0;
        missCount = 0;
        accessCounter = 0;
    }
    
    // Find entry in cache
    static int FindEntry(const char* device_name, uint64_t lba) {
        if (!cache) return -1;
        
        for (uint32_t i = 0; i < CACHE_SIZE; i++) {
            if (cache[i].valid && 
                cache[i].lba == lba && 
                kstrcmp(cache[i].device_name, device_name) == 0) {
                return i;
            }
        }
        return -1;
    }
    
    // Find LRU entry to evict
    static int FindLRU() {
        if (!cache) return 0;
        
        int oldest = 0;
        uint32_t minAccess = cache[0].access_count;
        
        for (uint32_t i = 1; i < CACHE_SIZE; i++) {
            if (!cache[i].valid) return i; // Empty slot
            if (cache[i].access_count < minAccess) {
                minAccess = cache[i].access_count;
                oldest = i;
            }
        }
        return oldest;
    }
    
    bool Get(const char* device_name, uint64_t lba, void* buffer, uint32_t sector_size) {
        int idx = FindEntry(device_name, lba);
        if (idx >= 0) {
            kmemcpy(buffer, cache[idx].data, sector_size);
            cache[idx].access_count = ++accessCounter;
            hitCount++;
            return true;
        }
        missCount++;
        return false;
    }
    
    void Put(const char* device_name, uint64_t lba, void* data, uint32_t sector_size) {
        if (!cache || sector_size > MAX_SECTOR_SIZE) return;
        
        // Check if already cached
        int idx = FindEntry(device_name, lba);
        if (idx < 0) {
            idx = FindLRU();
        }
        
        // Copy device name
        int i = 0;
        while (device_name[i] && i < 15) {
            cache[idx].device_name[i] = device_name[i];
            i++;
        }
        cache[idx].device_name[i] = 0;
        
        cache[idx].lba = lba;
        kmemcpy(cache[idx].data, data, sector_size);
        cache[idx].access_count = ++accessCounter;
        cache[idx].valid = true;
    }
    
    void Invalidate(const char* device_name, uint64_t lba) {
        int idx = FindEntry(device_name, lba);
        if (idx >= 0) {
            cache[idx].valid = false;
        }
    }
    
    void FlushDevice(const char* device_name) {
        if (!cache) return;
        
        for (uint32_t i = 0; i < CACHE_SIZE; i++) {
            if (cache[i].valid && kstrcmp(cache[i].device_name, device_name) == 0) {
                cache[i].valid = false;
            }
        }
    }
    
    uint32_t GetHitCount() { return hitCount; }
    uint32_t GetMissCount() { return missCount; }
}
