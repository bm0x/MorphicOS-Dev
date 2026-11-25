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
            return 0;
        }

        // Detect Protective MBR (0xEE) -> Parse UEFI GPT Header at LBA 1
        if (mbr->partitions[0].type == 0xEE) {
            struct __attribute__((packed)) GPTHeader {
                char signature[8];
                uint32_t revision;
                uint32_t header_size;
                uint32_t header_crc32;
                uint32_t reserved;
                uint64_t current_lba;
                uint64_t backup_lba;
                uint64_t first_usable_lba;
                uint64_t last_usable_lba;
                uint8_t  disk_guid[16];
                uint64_t part_entry_lba;
                uint32_t num_part_entries;
                uint32_t part_entry_size;
                uint32_t part_array_crc32;
            };
            struct __attribute__((packed)) GPTEntry {
                uint8_t  type_guid[16];
                uint8_t  unique_guid[16];
                uint64_t first_lba;
                uint64_t last_lba;
                uint64_t attributes;
                uint16_t name[36];
            };

            uint8_t gpt_hdr_buf[512];
            if (device->read_blocks(device, 1, 1, gpt_hdr_buf)) {
                GPTHeader* hdr = (GPTHeader*)gpt_hdr_buf;
                if (kstrncmp(hdr->signature, "EFI PART", 8) == 0 &&
                    hdr->part_entry_size >= sizeof(GPTEntry) &&
                    hdr->part_entry_size <= 512) {
                    uint32_t maxEntries = hdr->num_part_entries;
                    if (maxEntries > 128) maxEntries = 128;
                    uint32_t entriesPerSector = 512 / hdr->part_entry_size;
                    uint32_t sectorsToRead = (maxEntries + entriesPerSector - 1) / entriesPerSector;
                    uint8_t sectorBuf[512];
                    uint32_t gptFound = 0;

                    for (uint32_t s = 0; s < sectorsToRead && partitionCount < MAX_PARTITIONS; s++) {
                        if (!device->read_blocks(device, hdr->part_entry_lba + s, 1, sectorBuf)) break;
                        for (uint32_t e = 0; e < entriesPerSector && partitionCount < MAX_PARTITIONS; e++) {
                            GPTEntry* ge = (GPTEntry*)(sectorBuf + e * hdr->part_entry_size);
                            bool nonZero = false;
                            for (int b = 0; b < 16; b++) {
                                if (ge->type_guid[b] != 0) { nonZero = true; break; }
                            }
                            if (!nonZero || ge->last_lba < ge->first_lba) continue;

                            Partition* p = &partitions[partitionCount];
                            int nameLen = 0;
                            const char* devName = device->name;
                            while (*devName && nameLen < 11) p->name[nameLen++] = *devName++;
                            p->name[nameLen++] = 'p';
                            uint32_t pNum = gptFound + 1;
                            if (pNum >= 10) p->name[nameLen++] = (char)('0' + (pNum / 10));
                            p->name[nameLen++] = (char)('0' + (pNum % 10));
                            p->name[nameLen] = 0;

                            p->parent = device;
                            p->start_lba = ge->first_lba;
                            p->sector_count = (ge->last_lba - ge->first_lba) + 1;
                            p->type = PartitionType::UNKNOWN;
                            p->bootable = false;

                            partitionCount++;
                            gptFound++;
                        }
                    }
                    if (gptFound > 0) return gptFound;
                }
            }
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
