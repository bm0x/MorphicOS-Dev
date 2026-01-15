// FAT32 Filesystem Driver Implementation
// HAL-based implementation for MorphicOS

#include "fat32.h"
#include "../vfs.h"
#include "../../mm/heap.h"
#include "../../utils/std.h"
#include "../../hal/video/early_term.h"
#include "../../hal/serial/uart.h"

namespace FAT32 {
    // Forward declarations for IFileSystem vtable
    static int fs_read(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset);
    static int fs_write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset);
    static int fs_create(IFileSystem* self, const char* path, uint32_t mode);
    static int fs_remove(IFileSystem* self, const char* path);
    static int fs_mkdir(IFileSystem* self, const char* path);
    static int fs_rmdir(IFileSystem* self, const char* path);
    static int fs_stat(IFileSystem* self, const char* path, void* stat_buf);
    static int fs_readdir(IFileSystem* self, const char* path, void* entries, int max_entries);

    // Static filesystem template
    static IFileSystem fat32_template = {
        .name = "fat32",
        .read = fs_read,
        .write = fs_write,
        .create = fs_create,
        .remove = fs_remove,
        .rename = nullptr,
        .mkdir = fs_mkdir,
        .rmdir = fs_rmdir,
        .stat = fs_stat,
        .readdir = fs_readdir,
        .private_data = nullptr
    };

    void Init() {
        Mount::RegisterFS("fat32", &fat32_template);
        EarlyTerm::Print("[FAT32] Driver registered.\n");
    }

    // Convert cluster number to LBA
    static uint64_t ClusterToLBA(FAT32State* state, uint32_t cluster) {
        return state->data_start_lba + (uint64_t)(cluster - 2) * state->sectors_per_cluster;
    }

    // Read sectors from device
    static bool ReadSectors(FAT32State* state, uint64_t lba, uint32_t count, void* buffer) {
        return state->device->read_blocks(state->device, 
            state->partition_start + lba, count, buffer);
    }

    // Write sectors to device
    static bool WriteSectors(FAT32State* state, uint64_t lba, uint32_t count, const void* buffer) {
        return state->device->write_blocks(state->device,
            state->partition_start + lba, count, (void*)buffer);
    }

    // Read a FAT entry
    uint32_t GetNextCluster(FAT32State* state, uint32_t cluster) {
        if (cluster < 2 || cluster >= state->total_clusters + 2) {
            return FAT32_CLUSTER_EOF;
        }
        
        // Calculate which sector of the FAT contains this entry
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fat_offset / state->bytes_per_sector;
        uint32_t entry_offset = fat_offset % state->bytes_per_sector;
        
        // Read the FAT sector
        uint8_t* buffer = (uint8_t*)kmalloc(state->bytes_per_sector);
        if (!buffer) return FAT32_CLUSTER_EOF;
        
        if (!ReadSectors(state, state->fat_start_lba + fat_sector, 1, buffer)) {
            kfree(buffer);
            return FAT32_CLUSTER_EOF;
        }
        
        uint32_t value = *(uint32_t*)(buffer + entry_offset) & 0x0FFFFFFF;
        kfree(buffer);
        
        return value;
    }

    // Set a FAT entry
    static bool SetFATEntry(FAT32State* state, uint32_t cluster, uint32_t value) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fat_offset / state->bytes_per_sector;
        uint32_t entry_offset = fat_offset % state->bytes_per_sector;
        
        uint8_t* buffer = (uint8_t*)kmalloc(state->bytes_per_sector);
        if (!buffer) return false;
        
        // Read sector
        if (!ReadSectors(state, state->fat_start_lba + fat_sector, 1, buffer)) {
            kfree(buffer);
            return false;
        }
        
        // Modify entry (preserve high 4 bits)
        uint32_t* entry = (uint32_t*)(buffer + entry_offset);
        *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);
        
        // Write back to all FATs
        for (uint32_t i = 0; i < state->num_fats; i++) {
            uint64_t fat_lba = state->fat_start_lba + i * state->sectors_per_fat + fat_sector;
            WriteSectors(state, fat_lba, 1, buffer);
        }
        
        kfree(buffer);
        return true;
    }

    // Read a cluster
    bool ReadCluster(FAT32State* state, uint32_t cluster, void* buffer) {
        uint64_t lba = ClusterToLBA(state, cluster);
        return ReadSectors(state, lba, state->sectors_per_cluster, buffer);
    }

    // Write a cluster
    bool WriteCluster(FAT32State* state, uint32_t cluster, const void* buffer) {
        uint64_t lba = ClusterToLBA(state, cluster);
        return WriteSectors(state, lba, state->sectors_per_cluster, buffer);
    }

    // Allocate a free cluster
    uint32_t AllocateCluster(FAT32State* state) {
        // Scan FAT for free cluster (simple linear scan, could optimize)
        for (uint32_t c = 2; c < state->total_clusters + 2; c++) {
            uint32_t entry = GetNextCluster(state, c);
            if (entry == FAT32_CLUSTER_FREE) {
                // Mark as EOF (allocated but end of chain)
                SetFATEntry(state, c, FAT32_CLUSTER_EOF);
                
                // Clear the cluster
                uint8_t* zero = (uint8_t*)kmalloc(state->cluster_size);
                if (zero) {
                    kmemset(zero, 0, state->cluster_size);
                    WriteCluster(state, c, zero);
                    kfree(zero);
                }
                
                return c;
            }
        }
        return 0; // Disk full
    }

    // Parse 8.3 filename from directory entry
    static void Parse83Name(const FAT32DirEntry* entry, char* out) {
        int i = 0, j = 0;
        
        // Copy name (trim trailing spaces)
        for (i = 0; i < 8 && entry->name[i] != ' '; i++) {
            out[j++] = entry->name[i];
        }
        
        // Add dot and extension if present
        if (entry->ext[0] != ' ') {
            out[j++] = '.';
            for (i = 0; i < 3 && entry->ext[i] != ' '; i++) {
                out[j++] = entry->ext[i];
            }
        }
        
        out[j] = 0;
        
        // Convert to lowercase for consistency
        for (i = 0; out[i]; i++) {
            if (out[i] >= 'A' && out[i] <= 'Z') {
                out[i] = out[i] - 'A' + 'a';
            }
        }
    }

    // Find entry in a directory
    bool FindEntry(FAT32State* state, uint32_t dir_cluster, const char* name, FAT32FileInfo* out) {
        uint8_t* buffer = (uint8_t*)kmalloc(state->cluster_size);
        if (!buffer) return false;
        
        uint32_t cluster = dir_cluster;
        
        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF) {
            if (!ReadCluster(state, cluster, buffer)) {
                kfree(buffer);
                return false;
            }
            
            FAT32DirEntry* entries = (FAT32DirEntry*)buffer;
            int entries_per_cluster = state->cluster_size / sizeof(FAT32DirEntry);
            
            for (int i = 0; i < entries_per_cluster; i++) {
                // End of directory
                if (entries[i].name[0] == 0x00) {
                    kfree(buffer);
                    return false;
                }
                
                // Skip deleted entries
                if ((uint8_t)entries[i].name[0] == 0xE5) continue;
                
                // Skip LFN entries for now
                if ((entries[i].attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
                
                // Skip volume label
                if (entries[i].attr & FAT32_ATTR_VOLUME_ID) continue;
                
                // Parse the name
                char parsed_name[13];
                Parse83Name(&entries[i], parsed_name);
                
                // Case-insensitive compare
                if (kstricmp(parsed_name, name) == 0) {
                    // Found it!
                    int k = 0;
                    while (parsed_name[k] && k < 255) {
                        out->name[k] = parsed_name[k];
                        k++;
                    }
                    out->name[k] = 0;
                    
                    out->cluster = ((uint32_t)entries[i].cluster_high << 16) | entries[i].cluster_low;
                    out->size = entries[i].file_size;
                    out->attr = entries[i].attr;
                    out->is_directory = (entries[i].attr & FAT32_ATTR_DIRECTORY) != 0;
                    
                    kfree(buffer);
                    return true;
                }
            }
            
            cluster = GetNextCluster(state, cluster);
        }
        
        kfree(buffer);
        return false;
    }

    // Parse a full path and return file info
    bool ParsePath(FAT32State* state, const char* path, FAT32FileInfo* out) {
        // Start at root
        uint32_t current_cluster = state->root_cluster;
        
        // Handle root directory
        if (path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
            out->name[0] = '/';
            out->name[1] = 0;
            out->cluster = state->root_cluster;
            out->size = 0;
            out->attr = FAT32_ATTR_DIRECTORY;
            out->is_directory = true;
            return true;
        }
        
        // Skip leading slash
        if (*path == '/') path++;
        
        char component[64];
        
        while (*path) {
            // Extract next path component
            int i = 0;
            while (*path && *path != '/' && i < 63) {
                component[i++] = *path++;
            }
            component[i] = 0;
            
            // Skip trailing slash
            if (*path == '/') path++;
            
            // Find this component
            FAT32FileInfo info;
            if (!FindEntry(state, current_cluster, component, &info)) {
                return false;
            }
            
            // If more path remains, this must be a directory
            if (*path && !info.is_directory) {
                return false;
            }
            
            current_cluster = info.cluster;
            *out = info;
        }
        
        return true;
    }

    // Mount a FAT32 filesystem
    IFileSystem* Mount(IBlockDevice* device, uint64_t partition_start) {
        // Allocate state
        FAT32State* state = (FAT32State*)kmalloc(sizeof(FAT32State));
        if (!state) return nullptr;
        kmemset(state, 0, sizeof(FAT32State));
        
        state->device = device;
        state->partition_start = partition_start;
        
        // Read boot sector
        uint8_t* boot_sector = (uint8_t*)kmalloc(512);
        if (!boot_sector) {
            kfree(state);
            return nullptr;
        }
        
        if (!device->read_blocks(device, partition_start, 1, boot_sector)) {
            UART::Write("[FAT32] Failed to read boot sector\n");
            kfree(boot_sector);
            kfree(state);
            return nullptr;
        }
        
        FAT32BootSector* bpb = (FAT32BootSector*)boot_sector;
        
        // Validate signature
        if (*(uint16_t*)(boot_sector + 510) != FAT32_SIGNATURE) {
            UART::Write("[FAT32] Invalid boot sector signature\n");
            kfree(boot_sector);
            kfree(state);
            return nullptr;
        }
        
        // Validate FAT32 (sectors_per_fat_16 must be 0)
        if (bpb->sectors_per_fat_16 != 0) {
            UART::Write("[FAT32] Not a FAT32 filesystem (FAT12/16?)\n");
            kfree(boot_sector);
            kfree(state);
            return nullptr;
        }
        
        // Extract parameters
        state->bytes_per_sector = bpb->bytes_per_sector;
        state->sectors_per_cluster = bpb->sectors_per_cluster;
        state->reserved_sectors = bpb->reserved_sectors;
        state->num_fats = bpb->num_fats;
        state->sectors_per_fat = bpb->sectors_per_fat_32;
        state->root_cluster = bpb->root_cluster;
        
        // Calculate locations
        state->fat_start_lba = state->reserved_sectors;
        state->data_start_lba = state->fat_start_lba + (state->num_fats * state->sectors_per_fat);
        state->cluster_size = state->sectors_per_cluster * state->bytes_per_sector;
        
        uint32_t total_sectors = bpb->total_sectors_32;
        uint32_t data_sectors = total_sectors - state->data_start_lba;
        state->total_clusters = data_sectors / state->sectors_per_cluster;
        
        kfree(boot_sector);
        
        // Create filesystem instance
        IFileSystem* fs = (IFileSystem*)kmalloc(sizeof(IFileSystem));
        if (!fs) {
            kfree(state);
            return nullptr;
        }
        
        kmemcpy(fs, &fat32_template, sizeof(IFileSystem));
        fs->private_data = state;
        
        UART::Write("[FAT32] Mounted: ");
        UART::WriteDec(state->total_clusters);
        UART::Write(" clusters, ");
        UART::WriteDec(state->cluster_size);
        UART::Write(" bytes/cluster\n");
        
        return fs;
    }

    void Unmount(IFileSystem* fs) {
        if (fs && fs->private_data) {
            FAT32State* state = (FAT32State*)fs->private_data;
            if (state->fat_cache) kfree(state->fat_cache);
            kfree(state);
        }
        if (fs) kfree(fs);
    }

    // ================== IFileSystem Operations ==================

    static int fs_read(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state) return -1;
        
        FAT32FileInfo info;
        if (!ParsePath(state, path, &info)) {
            return -1; // File not found
        }
        
        if (info.is_directory) {
            return -1; // Cannot read directory as file
        }
        
        // Clamp read to file size
        if (offset >= info.size) return 0;
        if (offset + size > info.size) size = info.size - offset;
        
        uint8_t* cluster_buf = (uint8_t*)kmalloc(state->cluster_size);
        if (!cluster_buf) return -1;
        
        uint32_t cluster = info.cluster;
        size_t bytes_read = 0;
        size_t current_offset = 0;
        uint8_t* out = (uint8_t*)buf;
        
        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF && bytes_read < size) {
            // Skip clusters before offset
            if (current_offset + state->cluster_size <= offset) {
                current_offset += state->cluster_size;
                cluster = GetNextCluster(state, cluster);
                continue;
            }
            
            if (!ReadCluster(state, cluster, cluster_buf)) {
                kfree(cluster_buf);
                return bytes_read > 0 ? bytes_read : -1;
            }
            
            // Calculate how much to copy from this cluster
            size_t cluster_offset = 0;
            if (current_offset < offset) {
                cluster_offset = offset - current_offset;
            }
            
            size_t copy_size = state->cluster_size - cluster_offset;
            if (copy_size > size - bytes_read) {
                copy_size = size - bytes_read;
            }
            
            kmemcpy(out + bytes_read, cluster_buf + cluster_offset, copy_size);
            bytes_read += copy_size;
            current_offset += state->cluster_size;
            
            cluster = GetNextCluster(state, cluster);
        }
        
        kfree(cluster_buf);
        return bytes_read;
    }

    static int fs_write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset) {
        // TODO: Implement file write
        // 1. Parse path to find parent directory
        // 2. Find or create file entry
        // 3. Allocate clusters as needed
        // 4. Write data to clusters
        // 5. Update file size in directory entry
        return -1; // Not implemented yet
    }

    static int fs_create(IFileSystem* self, const char* path, uint32_t mode) {
        // TODO: Implement file creation
        return -1;
    }

    static int fs_remove(IFileSystem* self, const char* path) {
        // TODO: Implement file deletion
        return -1;
    }

    static int fs_mkdir(IFileSystem* self, const char* path) {
        // TODO: Implement directory creation
        return -1;
    }

    static int fs_rmdir(IFileSystem* self, const char* path) {
        // TODO: Implement directory removal
        return -1;
    }

    static int fs_stat(IFileSystem* self, const char* path, void* stat_buf) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state) return -1;
        
        FAT32FileInfo info;
        if (!ParsePath(state, path, &info)) {
            return -1;
        }
        
        // Fill stat buffer (using FileStat from vfs.h)
        FileStat* st = (FileStat*)stat_buf;
        kmemset(st, 0, sizeof(FileStat));
        
        int i = 0;
        while (info.name[i] && i < 63) {
            st->name[i] = info.name[i];
            i++;
        }
        st->name[i] = 0;
        
        st->type = info.is_directory ? NodeType::DIRECTORY : NodeType::FILE;
        st->size = info.size;
        
        return 0;
    }

    static int fs_readdir(IFileSystem* self, const char* path, void* entries, int max_entries) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state) return -1;
        
        FAT32FileInfo dir_info;
        if (!ParsePath(state, path, &dir_info)) {
            return -1;
        }
        
        if (!dir_info.is_directory) {
            return -1;
        }
        
        // Use the same DirEntry struct as syscall
        struct DirEntry {
            char name[64];
            uint32_t type;  // 0=file, 1=directory
            uint32_t size;
        };
        
        DirEntry* out = (DirEntry*)entries;
        int count = 0;
        
        uint8_t* cluster_buf = (uint8_t*)kmalloc(state->cluster_size);
        if (!cluster_buf) return -1;
        
        uint32_t cluster = dir_info.cluster;
        
        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF && count < max_entries) {
            if (!ReadCluster(state, cluster, cluster_buf)) break;
            
            FAT32DirEntry* dir_entries = (FAT32DirEntry*)cluster_buf;
            int entries_per_cluster = state->cluster_size / sizeof(FAT32DirEntry);
            
            for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
                // End of directory
                if (dir_entries[i].name[0] == 0x00) {
                    goto done;
                }
                
                // Skip deleted
                if ((uint8_t)dir_entries[i].name[0] == 0xE5) continue;
                
                // Skip LFN
                if ((dir_entries[i].attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) continue;
                
                // Skip volume label
                if (dir_entries[i].attr & FAT32_ATTR_VOLUME_ID) continue;
                
                // Skip . and ..
                if (dir_entries[i].name[0] == '.') continue;
                
                // Parse name
                char parsed_name[13];
                Parse83Name(&dir_entries[i], parsed_name);
                
                int j = 0;
                while (parsed_name[j] && j < 63) {
                    out[count].name[j] = parsed_name[j];
                    j++;
                }
                out[count].name[j] = 0;
                
                out[count].type = (dir_entries[i].attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;
                out[count].size = dir_entries[i].file_size;
                
                count++;
            }
            
            cluster = GetNextCluster(state, cluster);
        }
        
    done:
        kfree(cluster_buf);
        return count;
    }
}
