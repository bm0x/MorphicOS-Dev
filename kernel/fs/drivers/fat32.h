#pragma once

// FAT32 Filesystem Driver
// HAL-based implementation for MorphicOS

#include <stdint.h>
#include <stddef.h>
#include "../mount.h"
#include "../../hal/storage/block_device.h"

// FAT32 Constants
#define FAT32_SIGNATURE     0xAA55
#define FAT32_CLUSTER_FREE  0x00000000
#define FAT32_CLUSTER_EOF   0x0FFFFFF8
#define FAT32_CLUSTER_BAD   0x0FFFFFF7
#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

// FAT32 Boot Sector (BPB - BIOS Parameter Block)
struct __attribute__((packed)) FAT32BootSector {
    uint8_t  jmp[3];              // Jump instruction
    char     oem_name[8];         // OEM name
    uint16_t bytes_per_sector;    // Usually 512
    uint8_t  sectors_per_cluster; // Power of 2 (1, 2, 4, 8, etc.)
    uint16_t reserved_sectors;    // Before first FAT
    uint8_t  num_fats;            // Usually 2
    uint16_t root_entry_count;    // 0 for FAT32
    uint16_t total_sectors_16;    // 0 for FAT32
    uint8_t  media_type;          // 0xF8 for fixed disk
    uint16_t sectors_per_fat_16;  // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;    // Total sectors
    
    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;  // FAT size
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;        // First cluster of root directory
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;      // 0x29
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];          // "FAT32   "
};

// FAT32 Directory Entry (8.3 format)
struct __attribute__((packed)) FAT32DirEntry {
    char     name[8];             // Short name (padded with spaces)
    char     ext[3];              // Extension (padded with spaces)
    uint8_t  attr;                // Attributes
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;        // High 16 bits of cluster
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;         // Low 16 bits of cluster
    uint32_t file_size;
};

// Long Filename Entry
struct __attribute__((packed)) FAT32LFNEntry {
    uint8_t  order;               // Sequence number
    uint16_t name1[5];            // Characters 1-5 (UCS-2)
    uint8_t  attr;                // Always 0x0F
    uint8_t  type;                // Always 0
    uint8_t  checksum;            // Short name checksum
    uint16_t name2[6];            // Characters 6-11
    uint16_t cluster;             // Always 0
    uint16_t name3[2];            // Characters 12-13
};

// FAT32 Filesystem State
struct FAT32State {
    IBlockDevice* device;         // Block device
    uint64_t partition_start;     // Partition start LBA
    
    // From boot sector
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t total_clusters;
    
    // Calculated
    uint32_t fat_start_lba;       // First FAT sector
    uint32_t data_start_lba;      // First data sector
    uint32_t cluster_size;        // Bytes per cluster
    
    // Cache
    uint32_t* fat_cache;          // Cached FAT table (or portion)
    uint32_t fat_cache_size;      // Number of entries cached
};

// FAT32 Directory Entry Info (parsed)
struct FAT32FileInfo {
    char name[256];               // Full filename (including LFN)
    uint32_t cluster;             // First cluster
    uint32_t size;                // File size in bytes
    uint8_t attr;                 // Attributes
    bool is_directory;
};

// FAT32 Filesystem Driver
namespace FAT32 {
    // Initialize driver and register with mount system
    void Init();
    
    // Mount a FAT32 partition
    // device: block device
    // partition_start: LBA of partition start
    // Returns IFileSystem instance or nullptr on failure
    IFileSystem* Mount(IBlockDevice* device, uint64_t partition_start);
    
    // Unmount and free resources
    void Unmount(IFileSystem* fs);
    
    // IFileSystem operations (implemented internally)
    int Read(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset);
    int Write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset);
    int Create(IFileSystem* self, const char* path, uint32_t mode);
    int Remove(IFileSystem* self, const char* path);
    int Mkdir(IFileSystem* self, const char* path);
    int Rmdir(IFileSystem* self, const char* path);
    int Stat(IFileSystem* self, const char* path, void* stat_buf);
    int ReadDir(IFileSystem* self, const char* path, void* entries, int max_entries);
    
    // Internal helpers
    uint32_t GetNextCluster(FAT32State* state, uint32_t cluster);
    bool ReadCluster(FAT32State* state, uint32_t cluster, void* buffer);
    bool WriteCluster(FAT32State* state, uint32_t cluster, const void* buffer);
    uint32_t AllocateCluster(FAT32State* state);
    bool FindEntry(FAT32State* state, uint32_t dir_cluster, const char* name, FAT32FileInfo* out);
    bool ParsePath(FAT32State* state, const char* path, FAT32FileInfo* out);
}
