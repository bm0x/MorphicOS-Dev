#pragma once

#include "block_device.h"

// Partition types
enum class PartitionType {
    EMPTY,
    FAT12,
    FAT16,
    FAT32,
    LINUX,
    NTFS,
    UNKNOWN
};

// Partition entry
struct Partition {
    char name[16];          // "hd0p1", "hd0p2", etc.
    IBlockDevice* parent;   // Parent physical device
    uint64_t start_lba;     // Starting sector
    uint64_t sector_count;  // Number of sectors
    PartitionType type;
    bool bootable;
};

// MBR Partition Table Entry (on-disk format)
struct __attribute__((packed)) MBREntry {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
};

// MBR Structure (on-disk format)
struct __attribute__((packed)) MBR {
    uint8_t bootstrap[446];
    MBREntry partitions[4];
    uint16_t signature;     // Should be 0xAA55
};

// Partition Manager
namespace PartitionManager {
    // Parse partitions from a block device
    // Returns number of partitions found
    uint32_t ScanDevice(IBlockDevice* device);
    
    // Get partition by index
    Partition* GetPartition(uint32_t index);
    
    // Get total partition count
    uint32_t GetPartitionCount();
}
