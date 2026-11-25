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

    static void ExtractLFNPart(const FAT32LFNEntry* lfn, char lfn_buf[256]) {
        int seq = (int)(lfn->order & 0x3F);
        if (seq < 1 || seq > 19) return;
        if (lfn->order & 0x40) {
            kmemset(lfn_buf, 0, 256);
        }
        int base = (seq - 1) * 13;
        uint16_t chars[13];
        for (int i = 0; i < 5; i++) chars[i] = lfn->name1[i];
        for (int i = 0; i < 6; i++) chars[5 + i] = lfn->name2[i];
        for (int i = 0; i < 2; i++) chars[11 + i] = lfn->name3[i];

        for (int i = 0; i < 13 && (base + i) < 255; i++) {
            uint16_t ch = chars[i];
            if (ch == 0x0000 || ch == 0xFFFF) {
                lfn_buf[base + i] = '\0';
                break;
            }
            lfn_buf[base + i] = (ch < 128) ? (char)ch : '?';
        }
    }

    static void Format83Name(const char* input, char outName[8], char outExt[3]) {
        for (int i = 0; i < 8; i++) outName[i] = ' ';
        for (int i = 0; i < 3; i++) outExt[i] = ' ';
        if (!input) return;

        const char* dot = nullptr;
        for (const char* p = input; *p; p++) {
            if (*p == '.') dot = p;
        }

        int ni = 0;
        for (const char* p = input; *p && p != dot && ni < 8; p++) {
            char c = *p;
            if (c == ' ' || c == '.') continue;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            outName[ni++] = c;
        }
        if (ni == 0) outName[0] = '_';

        if (dot && *(dot + 1)) {
            int ei = 0;
            for (const char* p = dot + 1; *p && ei < 3; p++) {
                char c = *p;
                if (c == ' ' || c == '.') continue;
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                outExt[ei++] = c;
            }
        }
    }

    static bool SplitParentAndName(const char* path, char* parentOut, size_t maxParent, char* leafOut, size_t maxLeaf) {
        if (!path || !parentOut || !leafOut || maxParent < 2 || maxLeaf < 2) return false;
        while (*path == '/') path++;
        if (*path == '\0') return false;

        int len = (int)kstrlen(path);
        while (len > 0 && path[len - 1] == '/') len--;
        if (len == 0) return false;

        int lastSlash = -1;
        for (int i = 0; i < len; i++) {
            if (path[i] == '/') lastSlash = i;
        }

        if (lastSlash < 0) {
            parentOut[0] = '/';
            parentOut[1] = '\0';
            size_t copyLen = ((size_t)len < maxLeaf - 1) ? (size_t)len : (maxLeaf - 1);
            kmemcpy(leafOut, path, copyLen);
            leafOut[copyLen] = '\0';
            return true;
        }

        size_t pLen = ((size_t)lastSlash < maxParent - 1) ? (size_t)lastSlash : (maxParent - 1);
        kmemcpy(parentOut, path, pLen);
        parentOut[pLen] = '\0';

        const char* leafStart = path + lastSlash + 1;
        size_t lLen = (size_t)(len - (lastSlash + 1));
        if (lLen == 0) return false;
        if (lLen >= maxLeaf) lLen = maxLeaf - 1;
        kmemcpy(leafOut, leafStart, lLen);
        leafOut[lLen] = '\0';
        return true;
    }

    static void FreeClusterChain(FAT32State* state, uint32_t startCluster) {
        uint32_t cluster = startCluster;
        uint32_t guard = 0;
        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF && guard < state->total_clusters) {
            uint32_t next = GetNextCluster(state, cluster);
            SetFATEntry(state, cluster, FAT32_CLUSTER_FREE);
            cluster = next;
            guard++;
        }
    }

    // Find entry in a directory (supports both LFN and 8.3 short names)
    bool FindEntry(FAT32State* state, uint32_t dir_cluster, const char* name, FAT32FileInfo* out) {
        uint8_t* buffer = (uint8_t*)kmalloc(state->cluster_size);
        if (!buffer) return false;
        
        uint32_t cluster = dir_cluster;
        char lfn_buf[256];
        kmemset(lfn_buf, 0, sizeof(lfn_buf));
        
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
                if ((uint8_t)entries[i].name[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    continue;
                }
                
                // Accumulate LFN entries
                if ((entries[i].attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) {
                    ExtractLFNPart((const FAT32LFNEntry*)&entries[i], lfn_buf);
                    continue;
                }
                
                // Skip volume label
                if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_buf[0] = 0;
                    continue;
                }
                
                // Parse the 8.3 name
                char parsed_name[13];
                Parse83Name(&entries[i], parsed_name);
                const char* effective_name = lfn_buf[0] ? lfn_buf : parsed_name;
                
                // Case-insensitive compare against LFN or 8.3 short name
                if (kstricmp(effective_name, name) == 0 || kstricmp(parsed_name, name) == 0) {
                    int k = 0;
                    while (effective_name[k] && k < 255) {
                        out->name[k] = effective_name[k];
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
                lfn_buf[0] = 0;
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

    static bool InsertDirEntry(FAT32State* state, uint32_t dir_cluster, const FAT32DirEntry& newEntry) {
        uint8_t* buffer = (uint8_t*)kmalloc(state->cluster_size);
        if (!buffer) return false;

        uint32_t cluster = dir_cluster;
        uint32_t last_cluster = dir_cluster;
        int entries_per_cluster = state->cluster_size / sizeof(FAT32DirEntry);

        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF) {
            if (!ReadCluster(state, cluster, buffer)) {
                kfree(buffer);
                return false;
            }
            FAT32DirEntry* entries = (FAT32DirEntry*)buffer;
            for (int i = 0; i < entries_per_cluster; i++) {
                uint8_t first = (uint8_t)entries[i].name[0];
                if (first == 0x00 || first == 0xE5) {
                    entries[i] = newEntry;
                    if (first == 0x00 && (i + 1) < entries_per_cluster) {
                        entries[i + 1].name[0] = 0x00;
                    }
                    bool ok = WriteCluster(state, cluster, buffer);
                    kfree(buffer);
                    return ok;
                }
            }
            last_cluster = cluster;
            cluster = GetNextCluster(state, cluster);
        }

        // Directory clusters full: extend directory by 1 cluster
        uint32_t extra = AllocateCluster(state);
        if (extra == 0) {
            kfree(buffer);
            return false;
        }
        SetFATEntry(state, last_cluster, extra);
        kmemset(buffer, 0, state->cluster_size);
        FAT32DirEntry* entries = (FAT32DirEntry*)buffer;
        entries[0] = newEntry;
        bool ok = WriteCluster(state, extra, buffer);
        kfree(buffer);
        return ok;
    }

    static bool UpdateOrDeleteEntry(FAT32State* state, uint32_t dir_cluster, const char* target_name,
                                    bool delete_entry, uint32_t new_cluster, uint32_t new_size) {
        uint8_t* buffer = (uint8_t*)kmalloc(state->cluster_size);
        if (!buffer) return false;

        uint32_t cluster = dir_cluster;
        char lfn_buf[256];
        kmemset(lfn_buf, 0, sizeof(lfn_buf));
        int entries_per_cluster = state->cluster_size / sizeof(FAT32DirEntry);

        while (cluster >= 2 && cluster < FAT32_CLUSTER_EOF) {
            if (!ReadCluster(state, cluster, buffer)) {
                kfree(buffer);
                return false;
            }
            FAT32DirEntry* entries = (FAT32DirEntry*)buffer;
            int lfn_start_idx = -1;

            for (int i = 0; i < entries_per_cluster; i++) {
                if (entries[i].name[0] == 0x00) {
                    kfree(buffer);
                    return false;
                }
                if ((uint8_t)entries[i].name[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    lfn_start_idx = -1;
                    continue;
                }
                if ((entries[i].attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) {
                    if (lfn_start_idx < 0) lfn_start_idx = i;
                    ExtractLFNPart((const FAT32LFNEntry*)&entries[i], lfn_buf);
                    continue;
                }
                if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_buf[0] = 0;
                    lfn_start_idx = -1;
                    continue;
                }

                char parsed_name[13];
                Parse83Name(&entries[i], parsed_name);
                const char* effective_name = lfn_buf[0] ? lfn_buf : parsed_name;

                if (kstricmp(effective_name, target_name) == 0 || kstricmp(parsed_name, target_name) == 0) {
                    if (delete_entry) {
                        if (lfn_start_idx >= 0) {
                            for (int d = lfn_start_idx; d < i; d++) {
                                entries[d].name[0] = (char)0xE5;
                            }
                        }
                        entries[i].name[0] = (char)0xE5;
                    } else {
                        entries[i].cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
                        entries[i].cluster_low  = (uint16_t)(new_cluster & 0xFFFF);
                        entries[i].file_size    = new_size;
                    }
                    bool ok = WriteCluster(state, cluster, buffer);
                    kfree(buffer);
                    return ok;
                }
                lfn_buf[0] = 0;
                lfn_start_idx = -1;
            }
            cluster = GetNextCluster(state, cluster);
        }

        kfree(buffer);
        return false;
    }

    static int fs_create(IFileSystem* self, const char* path, uint32_t mode) {
        (void)mode;
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state || !path) return -1;

        char parentPath[128];
        char leafName[64];
        if (!SplitParentAndName(path, parentPath, sizeof(parentPath), leafName, sizeof(leafName))) {
            return -1;
        }

        FAT32FileInfo parentInfo;
        if (!ParsePath(state, parentPath, &parentInfo) || !parentInfo.is_directory) {
            return -1;
        }

        FAT32FileInfo existing;
        if (FindEntry(state, parentInfo.cluster, leafName, &existing)) {
            return -1; // Already exists
        }

        FAT32DirEntry entry;
        kmemset(&entry, 0, sizeof(entry));
        Format83Name(leafName, entry.name, entry.ext);
        entry.attr = FAT32_ATTR_ARCHIVE;
        entry.cluster_high = 0;
        entry.cluster_low = 0;
        entry.file_size = 0;

        return InsertDirEntry(state, parentInfo.cluster, entry) ? 0 : -1;
    }

    static int fs_write(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state || !path || (!buf && size > 0)) return -1;
        if (size == 0) return 0;

        char parentPath[128];
        char leafName[64];
        if (!SplitParentAndName(path, parentPath, sizeof(parentPath), leafName, sizeof(leafName))) {
            return -1;
        }

        FAT32FileInfo parentInfo;
        if (!ParsePath(state, parentPath, &parentInfo) || !parentInfo.is_directory) {
            return -1;
        }

        FAT32FileInfo fileInfo;
        if (!FindEntry(state, parentInfo.cluster, leafName, &fileInfo)) {
            if (fs_create(self, path, 0644) != 0) return -1;
            if (!FindEntry(state, parentInfo.cluster, leafName, &fileInfo)) return -1;
        }
        if (fileInfo.is_directory) return -1;

        uint32_t firstCluster = fileInfo.cluster;
        if (firstCluster < 2) {
            firstCluster = AllocateCluster(state);
            if (firstCluster == 0) return -1;
        }

        uint8_t* clusterBuf = (uint8_t*)kmalloc(state->cluster_size);
        if (!clusterBuf) return -1;

        uint32_t curCluster = firstCluster;
        size_t curClusterStart = 0;

        // Walk/extend chain up to starting cluster for 'offset'
        while (curClusterStart + state->cluster_size <= offset) {
            uint32_t next = GetNextCluster(state, curCluster);
            if (next < 2 || next >= FAT32_CLUSTER_EOF) {
                next = AllocateCluster(state);
                if (next == 0) {
                    kfree(clusterBuf);
                    return -1;
                }
                SetFATEntry(state, curCluster, next);
            }
            curCluster = next;
            curClusterStart += state->cluster_size;
        }

        const uint8_t* src = (const uint8_t*)buf;
        size_t written = 0;

        while (written < size) {
            size_t inClusterOff = (written == 0 && offset > curClusterStart) ? (offset - curClusterStart) : 0;
            size_t chunk = state->cluster_size - inClusterOff;
            if (chunk > (size - written)) chunk = size - written;

            if (chunk < state->cluster_size) {
                if (!ReadCluster(state, curCluster, clusterBuf)) {
                    kmemset(clusterBuf, 0, state->cluster_size);
                }
            }
            kmemcpy(clusterBuf + inClusterOff, src + written, chunk);
            if (!WriteCluster(state, curCluster, clusterBuf)) {
                kfree(clusterBuf);
                break;
            }

            written += chunk;
            curClusterStart += state->cluster_size;

            if (written < size) {
                uint32_t next = GetNextCluster(state, curCluster);
                if (next < 2 || next >= FAT32_CLUSTER_EOF) {
                    next = AllocateCluster(state);
                    if (next == 0) break;
                    SetFATEntry(state, curCluster, next);
                }
                curCluster = next;
            }
        }

        kfree(clusterBuf);

        uint32_t newSize = fileInfo.size;
        if (offset + written > newSize) {
            newSize = (uint32_t)(offset + written);
        }
        UpdateOrDeleteEntry(state, parentInfo.cluster, leafName, false, firstCluster, newSize);
        return (written > 0) ? (int)written : -1;
    }

    static int fs_remove(IFileSystem* self, const char* path) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state || !path) return -1;

        char parentPath[128];
        char leafName[64];
        if (!SplitParentAndName(path, parentPath, sizeof(parentPath), leafName, sizeof(leafName))) {
            return -1;
        }

        FAT32FileInfo parentInfo;
        if (!ParsePath(state, parentPath, &parentInfo) || !parentInfo.is_directory) return -1;

        FAT32FileInfo info;
        if (!FindEntry(state, parentInfo.cluster, leafName, &info) || info.is_directory) return -1;

        if (info.cluster >= 2) {
            FreeClusterChain(state, info.cluster);
        }
        return UpdateOrDeleteEntry(state, parentInfo.cluster, leafName, true, 0, 0) ? 0 : -1;
    }

    static int fs_mkdir(IFileSystem* self, const char* path) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state || !path) return -1;

        char parentPath[128];
        char leafName[64];
        if (!SplitParentAndName(path, parentPath, sizeof(parentPath), leafName, sizeof(leafName))) {
            return -1;
        }

        FAT32FileInfo parentInfo;
        if (!ParsePath(state, parentPath, &parentInfo) || !parentInfo.is_directory) return -1;

        FAT32FileInfo existing;
        if (FindEntry(state, parentInfo.cluster, leafName, &existing)) return -1;

        uint32_t dirCluster = AllocateCluster(state);
        if (dirCluster == 0) return -1;

        uint8_t* clusterBuf = (uint8_t*)kmalloc(state->cluster_size);
        if (!clusterBuf) {
            SetFATEntry(state, dirCluster, FAT32_CLUSTER_FREE);
            return -1;
        }
        kmemset(clusterBuf, 0, state->cluster_size);
        FAT32DirEntry* dotEntries = (FAT32DirEntry*)clusterBuf;

        // "." entry
        for (int i = 0; i < 8; i++) dotEntries[0].name[i] = (i == 0) ? '.' : ' ';
        for (int i = 0; i < 3; i++) dotEntries[0].ext[i] = ' ';
        dotEntries[0].attr = FAT32_ATTR_DIRECTORY;
        dotEntries[0].cluster_high = (uint16_t)((dirCluster >> 16) & 0xFFFF);
        dotEntries[0].cluster_low  = (uint16_t)(dirCluster & 0xFFFF);

        // ".." entry
        uint32_t dotDotCluster = (parentInfo.cluster == state->root_cluster) ? 0 : parentInfo.cluster;
        for (int i = 0; i < 8; i++) dotEntries[1].name[i] = (i < 2) ? '.' : ' ';
        for (int i = 0; i < 3; i++) dotEntries[1].ext[i] = ' ';
        dotEntries[1].attr = FAT32_ATTR_DIRECTORY;
        dotEntries[1].cluster_high = (uint16_t)((dotDotCluster >> 16) & 0xFFFF);
        dotEntries[1].cluster_low  = (uint16_t)(dotDotCluster & 0xFFFF);

        bool wroteInit = WriteCluster(state, dirCluster, clusterBuf);
        kfree(clusterBuf);
        if (!wroteInit) {
            SetFATEntry(state, dirCluster, FAT32_CLUSTER_FREE);
            return -1;
        }

        FAT32DirEntry newDir;
        kmemset(&newDir, 0, sizeof(newDir));
        Format83Name(leafName, newDir.name, newDir.ext);
        newDir.attr = FAT32_ATTR_DIRECTORY;
        newDir.cluster_high = (uint16_t)((dirCluster >> 16) & 0xFFFF);
        newDir.cluster_low  = (uint16_t)(dirCluster & 0xFFFF);
        newDir.file_size    = 0;

        if (!InsertDirEntry(state, parentInfo.cluster, newDir)) {
            SetFATEntry(state, dirCluster, FAT32_CLUSTER_FREE);
            return -1;
        }
        return 0;
    }

    static int fs_rmdir(IFileSystem* self, const char* path) {
        FAT32State* state = (FAT32State*)self->private_data;
        if (!state || !path) return -1;

        char parentPath[128];
        char leafName[64];
        if (!SplitParentAndName(path, parentPath, sizeof(parentPath), leafName, sizeof(leafName))) {
            return -1;
        }

        FAT32FileInfo parentInfo;
        if (!ParsePath(state, parentPath, &parentInfo) || !parentInfo.is_directory) return -1;

        FAT32FileInfo dirInfo;
        if (!FindEntry(state, parentInfo.cluster, leafName, &dirInfo) || !dirInfo.is_directory) return -1;

        // Ensure directory is empty
        struct TmpEntry { char name[64]; uint32_t type; uint32_t size; } tmp;
        if (fs_readdir(self, path, &tmp, 1) > 0) {
            return -1; // Not empty
        }

        if (dirInfo.cluster >= 2) {
            FreeClusterChain(state, dirInfo.cluster);
        }
        return UpdateOrDeleteEntry(state, parentInfo.cluster, leafName, true, 0, 0) ? 0 : -1;
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
        char lfn_buf[256];
        kmemset(lfn_buf, 0, sizeof(lfn_buf));
        
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
                if ((uint8_t)dir_entries[i].name[0] == 0xE5) {
                    lfn_buf[0] = 0;
                    continue;
                }
                
                // Accumulate LFN
                if ((dir_entries[i].attr & FAT32_ATTR_LFN) == FAT32_ATTR_LFN) {
                    ExtractLFNPart((const FAT32LFNEntry*)&dir_entries[i], lfn_buf);
                    continue;
                }
                
                // Skip volume label
                if (dir_entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_buf[0] = 0;
                    continue;
                }
                
                // Skip . and ..
                if (dir_entries[i].name[0] == '.') {
                    lfn_buf[0] = 0;
                    continue;
                }
                
                // Parse name (prefer LFN if available)
                char parsed_name[13];
                Parse83Name(&dir_entries[i], parsed_name);
                const char* effective_name = lfn_buf[0] ? lfn_buf : parsed_name;
                
                int j = 0;
                while (effective_name[j] && j < 63) {
                    out[count].name[j] = effective_name[j];
                    j++;
                }
                out[count].name[j] = 0;
                lfn_buf[0] = 0;
                
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
