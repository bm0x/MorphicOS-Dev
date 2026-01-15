// MountManager - Automatic Filesystem Detection and Mounting
// Scans storage devices for known filesystems and mounts them

#include "mount_manager.h"
#include "vfs.h"
#include "mount.h"
#include "drivers/fat32.h"
#include "../hal/storage/block_device.h"
#include "../hal/video/early_term.h"
#include "../hal/serial/uart.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/verbose.h"
#include "../hal/storage/partition.h"
#include "drivers/ntfs.h"

namespace MountManager {
    static const int MAX_MOUNTS = 8;
    
    static MountInfo mounts[MAX_MOUNTS];
    static int mountCount = 0;
    
    // FAT32 signature check
    static bool IsFAT32(IBlockDevice* device, uint64_t partition_start) {
        uint8_t* boot_sector = (uint8_t*)kmalloc(512);
        if (!boot_sector) return false;
        
        if (!device->read_blocks(device, partition_start, 1, boot_sector)) {
            kfree(boot_sector);
            return false;
        }
        
        // Check boot sector signature (0x55AA at offset 510)
        if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
            kfree(boot_sector);
            return false;
        }
        
        // Check FAT32 specific: sectors_per_fat_16 must be 0
        uint16_t sectors_per_fat_16 = *(uint16_t*)(boot_sector + 22);
        if (sectors_per_fat_16 != 0) {
            kfree(boot_sector);
            return false; // FAT12/16, not FAT32
        }
        
        // Check filesystem type string at offset 82 ("FAT32   ")
        bool is_fat32 = (boot_sector[82] == 'F' && boot_sector[83] == 'A' && 
                         boot_sector[84] == 'T' && boot_sector[85] == '3' &&
                         boot_sector[86] == '2');
        
        kfree(boot_sector);
        return is_fat32;
    }
    
    void Init() {
        mountCount = 0;
        for (int i = 0; i < MAX_MOUNTS; i++) {
            kmemset(&mounts[i], 0, sizeof(MountInfo));
        }
        
        // Add initrd as first mount (always present)
        const char* p = "/initrd";
        const char* t = "initrd";
        for (int i = 0; p[i] && i < 31; i++) mounts[0].path[i] = p[i];
        for (int i = 0; t[i] && i < 15; i++) mounts[0].fstype[i] = t[i];
        mounts[0].device[0] = 0; // No device for initrd
        mountCount = 1;
        
        UART::Write("[MountManager] Initialized.\n");
    }
    
    int ScanAndMount() {
        int mounted = 0;
        
        UART::Write("[MountManager] Scanning storage devices...\n");
        
        uint32_t deviceCount = StorageManager::GetDeviceCount();
        UART::Write("[MountManager] Found ");
        UART::WriteDec(deviceCount);
        UART::Write(" storage devices.\n");
        
        for (uint32_t i = 0; i < deviceCount; i++) {
            IBlockDevice* device = StorageManager::GetDeviceByIndex(i);
            if (!device) continue;
            
            UART::Write("[MountManager] Checking device: ");
            UART::Write(device->name);
            UART::Write("\n");
            
            // 1. Scan for MBR Partitions
            int partitionsFound = PartitionManager::ScanDevice(device);
            bool mountedPartition = false;
            
            if (partitionsFound > 0) {
                 UART::Write("[MountManager] Found partitions: ");
                 UART::WriteDec(partitionsFound);
                 UART::Write("\n");
                 
                 for (uint32_t p = 0; p < PartitionManager::GetPartitionCount(); p++) {
                     Partition* part = PartitionManager::GetPartition(p);
                     if (part->parent != device) continue;
                     
                     // Check for FAT32 on partition
                     if (IsFAT32(device, part->start_lba)) {
                         UART::Write("[MountManager] FAT32 detected on ");
                         UART::Write(part->name);
                         UART::Write("\n");
                         
                         // Mount FAT32
                         IFileSystem* fs = FAT32::Mount(device, part->start_lba);
                         if (fs) {
                             // ... (Mount logic same as before) ...
                             // Inline duplication or refactor? Let's just duplicate logic for safety/speed
                             char mount_path[32];
                             if (mounted == 0) {
                                 const char* src = "/data";
                                 for(int k=0; k<6; k++) mount_path[k] = src[k];
                             } else {
                                 char num[8];
                                 kitoa(mounted, num, 10);
                                 mount_path[0]='/'; mount_path[1]='d'; mount_path[2]='i'; mount_path[3]='s'; mount_path[4]='k';
                                 int z=0; while(num[z]) mount_path[5+z] = num[z++];
                                 mount_path[5+z] = 0;
                             }

                             if (Mount::AddMount(mount_path, fs)) {
                                 if (mountCount < MAX_MOUNTS) {
                                     int k=0; while(mount_path[k]) { mounts[mountCount].path[k] = mount_path[k]; k++; } mounts[mountCount].path[k]=0;
                                     const char* ft = "fat32"; k=0; while(ft[k]) { mounts[mountCount].fstype[k] = ft[k]; k++; } mounts[mountCount].fstype[k]=0;
                                      k=0; while(part->name[k]) { mounts[mountCount].device[k] = part->name[k]; k++; } mounts[mountCount].device[k]=0;
                                     mountCount++;
                                 }
                                 mounted++;
                                 mountedPartition = true;
                                 Verbose::OK("MOUNT", mount_path);
                             }
                         }
                     } 
                     // Check for NTFS
                     else {
                         IFileSystem* ntfs = NTFS::Mount(device, part->start_lba);
                         if (ntfs) {
                             UART::Write("[MountManager] NTFS detected on ");
                             UART::Write(part->name);
                             UART::Write("\n");
                             
                             char mount_path[32];
                             if (mounted == 0) {
                                 const char* src = "/data";
                                 for(int k=0; k<6; k++) mount_path[k] = src[k];
                             } else {
                                 char num[8];
                                 kitoa(mounted, num, 10);
                                 mount_path[0]='/'; mount_path[1]='d'; mount_path[2]='i'; mount_path[3]='s'; mount_path[4]='k';
                                 int z=0; while(num[z]) mount_path[5+z] = num[z++];
                                 mount_path[5+z] = 0;
                             }
                             
                             if (Mount::AddMount(mount_path, ntfs)) {
                                 if (mountCount < MAX_MOUNTS) {
                                     int k=0; while(mount_path[k]) { mounts[mountCount].path[k] = mount_path[k]; k++; } mounts[mountCount].path[k]=0;
                                     const char* ft = "ntfs"; k=0; while(ft[k]) { mounts[mountCount].fstype[k] = ft[k]; k++; } mounts[mountCount].fstype[k]=0;
                                     k=0; while(part->name[k]) { mounts[mountCount].device[k] = part->name[k]; k++; } mounts[mountCount].device[k]=0;
                                     mountCount++;
                                 }
                                 mounted++;
                                 mountedPartition = true;
                                 Verbose::OK("MOUNT", mount_path);
                             }
                         }
                     }
                 }
            }
            
            // 2. Fallback: Check raw device if no partitions mounted
            if (!mountedPartition && IsFAT32(device, 0)) {
                UART::Write("[MountManager] FAT32 detected on RAW device ");
                UART::Write(device->name);
                UART::Write("!\n");
                
                IFileSystem* fs = FAT32::Mount(device, 0);
                if (fs) {
                    const char* mount_path = "/data";
                    if (Mount::AddMount(mount_path, fs)) {
                        if (mountCount < MAX_MOUNTS) {
                             int k=0; while(mount_path[k]) { mounts[mountCount].path[k] = mount_path[k]; k++; } mounts[mountCount].path[k]=0;
                             const char* ft = "fat32"; k=0; while(ft[k]) { mounts[mountCount].fstype[k] = ft[k]; k++; } mounts[mountCount].fstype[k]=0;
                             k=0; while(device->name[k]) { mounts[mountCount].device[k] = device->name[k]; k++; } mounts[mountCount].device[k]=0;
                             mountCount++;
                        }
                        mounted++;
                        Verbose::OK("MOUNT", mount_path);
                    }
                }
            }
        }
        
        return mounted;
    }
    
    int GetMountCount() {
        return mountCount;
    }
    
    bool GetMountInfo(int index, MountInfo* info) {
        if (index < 0 || index >= mountCount || !info) return false;
        *info = mounts[index];
        return true;
    }
}
