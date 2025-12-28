#pragma once

#include <stdint.h>

// Block Device Geometry
struct BlockGeometry {
    uint32_t sector_size;      // Bytes per sector (usually 512)
    uint64_t total_sectors;    // Total sector count
    uint64_t total_bytes;      // Total device size
};

// Forward declaration
struct IBlockDevice;

// Function pointer types for block operations
typedef bool (*BlockReadFunc)(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
typedef bool (*BlockWriteFunc)(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);

// Block Device Interface
// Storage drivers (AHCI, IDE, USB, RAMDisk) implement this
struct IBlockDevice {
    char name[16];             // Device ID: "hd0", "sd0", "rd0"
    BlockGeometry geometry;
    void* driver_data;         // Driver-specific context
    
    // Function pointers (HAL pattern - bus agnostic)
    BlockReadFunc read_blocks;
    BlockWriteFunc write_blocks;
};

// Storage Manager - Central registry for block devices
namespace StorageManager {
    void Init();
    
    // Register a block device with the system
    void RegisterDevice(IBlockDevice* device);
    
    // Get device by name (e.g., "hd0", "rd0")
    IBlockDevice* GetDevice(const char* name);
    
    // Get device by index
    IBlockDevice* GetDeviceByIndex(uint32_t index);
    
    // Get total registered device count
    uint32_t GetDeviceCount();
    
    // High-level API (bus-agnostic)
    // Kernel calls these - doesn't know about underlying hardware
    bool ReadSectors(const char* device_name, uint64_t lba, uint32_t count, void* buffer);
    bool WriteSectors(const char* device_name, uint64_t lba, uint32_t count, void* buffer);
}
