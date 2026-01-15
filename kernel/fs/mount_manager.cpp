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
            
            // For now, check device directly (no partition table parsing)
            // Assume the device is a raw FAT32 filesystem (like debug_disk.img)
            if (IsFAT32(device, 0)) {
                UART::Write("[MountManager] FAT32 detected on ");
                UART::Write(device->name);
                UART::Write("!\n");
                
                // Mount the FAT32 filesystem
                IFileSystem* fs = FAT32::Mount(device, 0);
                if (fs) {
                    // Determine mount point based on device name
                    const char* mount_path = "/data";
                    
                    // Register with mount system
                    if (Mount::AddMount(mount_path, fs)) {
                        // Record in our mount list
                        if (mountCount < MAX_MOUNTS) {
                            const char* mp = mount_path;
                            for (int j = 0; mp[j] && j < 31; j++) mounts[mountCount].path[j] = mp[j];
                            mounts[mountCount].path[31] = 0;
                            
                            const char* ft = "fat32";
                            for (int j = 0; ft[j] && j < 15; j++) mounts[mountCount].fstype[j] = ft[j];
                            mounts[mountCount].fstype[15] = 0;
                            
                            for (int j = 0; device->name[j] && j < 15; j++) mounts[mountCount].device[j] = device->name[j];
                            mounts[mountCount].device[15] = 0;
                            
                            mountCount++;
                        }
                        
                        mounted++;
                        EarlyTerm::Print("[MountManager] Mounted ");
                        EarlyTerm::Print(device->name);
                        EarlyTerm::Print(" as ");
                        EarlyTerm::Print(mount_path);
                        EarlyTerm::Print("\n");
                    } else {
                        UART::Write("[MountManager] Failed to add mount point!\n");
                        FAT32::Unmount(fs);
                    }
                } else {
                    UART::Write("[MountManager] FAT32::Mount failed!\n");
                }
            }
        }
        
        UART::Write("[MountManager] Mounted ");
        UART::WriteDec(mounted);
        UART::Write(" filesystem(s).\n");
        
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
