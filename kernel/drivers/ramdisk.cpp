#include "../hal/storage/block_device.h"
#include "../hal/device_registry.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

// RAMDisk Driver
// Implements IBlockDevice for testing without real hardware

namespace RAMDisk {
    static const uint32_t SECTOR_SIZE = 512;
    static const uint32_t SECTOR_COUNT = 256;  // 128KB RAMDisk (fits in heap)
    
    static uint8_t* diskData = nullptr;
    
    // Read sectors from RAMDisk
    static bool ReadBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!diskData) return false;
        if (lba + count > SECTOR_COUNT) return false;
        
        kmemcpy(buffer, diskData + (lba * SECTOR_SIZE), count * SECTOR_SIZE);
        return true;
    }
    
    // Write sectors to RAMDisk
    static bool WriteBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!diskData) return false;
        if (lba + count > SECTOR_COUNT) return false;
        
        kmemcpy(diskData + (lba * SECTOR_SIZE), buffer, count * SECTOR_SIZE);
        return true;
    }
    
    // Device descriptor
    static IBlockDevice device;
    
    void Init() {
        // Allocate disk data
        diskData = (uint8_t*)kmalloc(SECTOR_SIZE * SECTOR_COUNT);
        if (!diskData) {
            EarlyTerm::Print("[RAMDisk] ERROR: Cannot allocate memory!\n");
            return;
        }
        
        // Zero out the disk
        kmemset(diskData, 0, SECTOR_SIZE * SECTOR_COUNT);
        
        // Setup device descriptor
        device.name[0] = 'r';
        device.name[1] = 'd';
        device.name[2] = '0';
        device.name[3] = 0;
        
        device.geometry.sector_size = SECTOR_SIZE;
        device.geometry.total_sectors = SECTOR_COUNT;
        device.geometry.total_bytes = SECTOR_SIZE * SECTOR_COUNT;
        
        device.driver_data = diskData;
        device.read_blocks = ReadBlocks;
        device.write_blocks = WriteBlocks;
        
        // Register with HAL
        DeviceRegistry::Register(DeviceType::STORAGE, &device);
    }
    
    IBlockDevice* GetDevice() {
        return &device;
    }
}
