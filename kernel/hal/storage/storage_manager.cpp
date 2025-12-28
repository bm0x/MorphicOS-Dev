#include "block_device.h"
#include "buffer_cache.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../video/early_term.h"

namespace StorageManager {
    static const uint32_t MAX_DEVICES = 16;
    
    static IBlockDevice* devices[MAX_DEVICES];
    static uint32_t deviceCount = 0;
    
    void Init() {
        for (uint32_t i = 0; i < MAX_DEVICES; i++) {
            devices[i] = nullptr;
        }
        deviceCount = 0;
        
        BufferCache::Init();
        
        EarlyTerm::Print("[HAL] Storage Manager Initialized.\n");
    }
    
    void RegisterDevice(IBlockDevice* device) {
        if (!device) return;
        if (deviceCount >= MAX_DEVICES) {
            EarlyTerm::Print("[HAL] ERROR: Max storage devices reached!\n");
            return;
        }
        
        devices[deviceCount++] = device;
        
        EarlyTerm::Print("[HAL] Storage: ");
        EarlyTerm::Print(device->name);
        EarlyTerm::Print(" (");
        EarlyTerm::PrintDec(device->geometry.total_bytes / 1024);
        EarlyTerm::Print(" KB) registered.\n");
    }
    
    IBlockDevice* GetDevice(const char* name) {
        for (uint32_t i = 0; i < deviceCount; i++) {
            if (devices[i] && kstrcmp(devices[i]->name, name) == 0) {
                return devices[i];
            }
        }
        return nullptr;
    }
    
    IBlockDevice* GetDeviceByIndex(uint32_t index) {
        if (index < deviceCount) {
            return devices[index];
        }
        return nullptr;
    }
    
    uint32_t GetDeviceCount() {
        return deviceCount;
    }
    
    // High-level bus-agnostic read
    bool ReadSectors(const char* device_name, uint64_t lba, uint32_t count, void* buffer) {
        IBlockDevice* dev = GetDevice(device_name);
        if (!dev || !dev->read_blocks) return false;
        
        uint8_t* buf = (uint8_t*)buffer;
        uint32_t sector_size = dev->geometry.sector_size;
        
        for (uint32_t i = 0; i < count; i++) {
            // Check cache first (Swift optimization)
            if (BufferCache::Get(device_name, lba + i, buf, sector_size)) {
                buf += sector_size;
                continue;
            }
            
            // Cache miss - read from device
            if (!dev->read_blocks(dev, lba + i, 1, buf)) {
                return false;
            }
            
            // Cache the sector for next time
            BufferCache::Put(device_name, lba + i, buf, sector_size);
            buf += sector_size;
        }
        
        return true;
    }
    
    // High-level bus-agnostic write
    bool WriteSectors(const char* device_name, uint64_t lba, uint32_t count, void* buffer) {
        IBlockDevice* dev = GetDevice(device_name);
        if (!dev || !dev->write_blocks) return false;
        
        // Write-through: invalidate cache entries
        for (uint32_t i = 0; i < count; i++) {
            BufferCache::Invalidate(device_name, lba + i);
        }
        
        // Write to device
        return dev->write_blocks(dev, lba, count, buffer);
    }
}
