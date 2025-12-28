#include "partition.h"
#include "block_device.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../video/early_term.h"

namespace PartitionManager {
    static const uint32_t MAX_PARTITIONS = 32;
    
    static Partition partitions[MAX_PARTITIONS];
    static uint32_t partitionCount = 0;
    
    // Convert MBR type to PartitionType
    static PartitionType MBRTypeToPartitionType(uint8_t type) {
        switch (type) {
            case 0x00: return PartitionType::EMPTY;
            case 0x01: return PartitionType::FAT12;
            case 0x04:
            case 0x06:
            case 0x0E: return PartitionType::FAT16;
            case 0x0B:
            case 0x0C: return PartitionType::FAT32;
            case 0x07: return PartitionType::NTFS;
            case 0x83: return PartitionType::LINUX;
            default:   return PartitionType::UNKNOWN;
        }
    }
    
    uint32_t ScanDevice(IBlockDevice* device) {
        if (!device || !device->read_blocks) return 0;
        
        // Read MBR (first sector)
        uint8_t mbr_buffer[512];
        if (!device->read_blocks(device, 0, 1, mbr_buffer)) {
            return 0;
        }
        
        MBR* mbr = (MBR*)mbr_buffer;
        
        // Check MBR signature
        if (mbr->signature != 0xAA55) {
            // Not a valid MBR
            return 0;
        }
        
        uint32_t found = 0;
        
        for (int i = 0; i < 4; i++) {
            if (mbr->partitions[i].type != 0x00 && 
                mbr->partitions[i].sector_count > 0) {
                
                if (partitionCount >= MAX_PARTITIONS) break;
                
                Partition* p = &partitions[partitionCount];
                
                // Generate partition name (e.g., "hd0p1")
                int nameLen = 0;
                const char* devName = device->name;
                while (*devName && nameLen < 12) {
                    p->name[nameLen++] = *devName++;
                }
                p->name[nameLen++] = 'p';
                p->name[nameLen++] = '1' + i;
                p->name[nameLen] = 0;
                
                p->parent = device;
                p->start_lba = mbr->partitions[i].lba_first;
                p->sector_count = mbr->partitions[i].sector_count;
                p->type = MBRTypeToPartitionType(mbr->partitions[i].type);
                p->bootable = (mbr->partitions[i].status == 0x80);
                
                partitionCount++;
                found++;
                
                EarlyTerm::Print("[Partition] ");
                EarlyTerm::Print(p->name);
                EarlyTerm::Print(" LBA:");
                EarlyTerm::PrintDec(p->start_lba);
                EarlyTerm::Print(" Size:");
                EarlyTerm::PrintDec((p->sector_count * 512) / 1024);
                EarlyTerm::Print("KB\n");
            }
        }
        
        return found;
    }
    
    Partition* GetPartition(uint32_t index) {
        if (index < partitionCount) {
            return &partitions[index];
        }
        return nullptr;
    }
    
    uint32_t GetPartitionCount() {
        return partitionCount;
    }
}
