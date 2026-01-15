#pragma once

#include "../mount.h"
#include "../../hal/storage/block_device.h"

namespace NTFS {

    // NTFS Boot Sector
    struct BootSector {
        uint8_t jump[3];
        char oem_id[8];
        uint16_t bytes_per_sector;
        uint8_t sectors_per_cluster;
        uint16_t reserved_sectors;
        uint8_t zero1[3];
        uint16_t zero2;
        uint8_t media_descriptor;
        uint16_t zero3;
        uint16_t sectors_per_track;
        uint16_t heads;
        uint32_t hidden_sectors;
        uint32_t zero4;
        uint32_t zero5;
        uint64_t total_sectors;
        uint64_t mft_cluster;
        uint64_t mft_mirror_cluster;
        int8_t   clusters_per_record;
        uint8_t  zero6[3];
        int8_t   clusters_per_index;
        uint8_t  zero7[3];
        uint64_t serial_number;
        uint32_t checksum;
        uint8_t  code[426];
        uint16_t signature;
    } __attribute__((packed));

    // Internal State
    struct NTFSState {
        IBlockDevice* device;
        uint64_t partition_start;
        BootSector bootSector;
    };

    // Driver Functions
    IFileSystem* Mount(IBlockDevice* device, uint64_t start_lba);
    
    // IFileSystem Interface Implementation
    int Read(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset);
    int Write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset);
    int Readdir(IFileSystem* self, const char* path, void* entries, int max_entries);
    // Add others if needed (stat, etc)
}
