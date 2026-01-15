#include "mount.h"
#include "../hal/video/early_term.h"
#include "../utils/std.h"

namespace Mount {
    static MountPoint mounts[MAX_MOUNT_POINTS];
    static int mountCount = 0;
    
    // Registered filesystem drivers
    static IFileSystem* registeredFS[16];
    static int fsCount = 0;
    
    void Init() {
        mountCount = 0;
        fsCount = 0;
        
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            mounts[i].active = false;
            mounts[i].fs = nullptr;
        }
        
        EarlyTerm::Print("[Mount] System initialized.\n");
    }
    
    bool RegisterFS(const char* name, IFileSystem* fs_template) {
        if (fsCount >= 16) return false;
        
        registeredFS[fsCount++] = fs_template;
        return true;
    }
    
    IFileSystem* GetFSByName(const char* name) {
        for (int i = 0; i < fsCount; i++) {
            if (kstrcmp(registeredFS[i]->name, name) == 0) {
                return registeredFS[i];
            }
        }
        return nullptr;
    }
    
    bool Mount(const char* device, const char* path, const char* fs_type, uint32_t flags) {
        if (mountCount >= MAX_MOUNT_POINTS) {
            EarlyTerm::Print("[Mount] Error: Max mount points reached.\n");
            return false;
        }
        
        // Find an empty slot
        int slot = -1;
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (!mounts[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot < 0) return false;
        
        // Get filesystem driver
        IFileSystem* fs = GetFSByName(fs_type);
        if (!fs) {
            EarlyTerm::Print("[Mount] Error: Unknown filesystem: ");
            EarlyTerm::Print(fs_type);
            EarlyTerm::Print("\n");
            return false;
        }
        
        // Setup mount point
        MountPoint* mp = &mounts[slot];
        
        // Copy strings safely
        int i = 0;
        if (path) {
            while (path[i] && i < MAX_PATH_LENGTH - 1) {
                mp->path[i] = path[i];
                i++;
            }
        }
        mp->path[i] = 0;
        
        i = 0;
        if (device) {
            while (device[i] && i < MAX_DEVICE_LENGTH - 1) {
                mp->device[i] = device[i];
                i++;
            }
        }
        mp->device[i] = 0;
        
        i = 0;
        while (fs_type[i] && i < MAX_FSTYPE_LENGTH - 1) {
            mp->fs_type[i] = fs_type[i];
            i++;
        }
        mp->fs_type[i] = 0;
        
        mp->flags = flags;
        mp->fs = fs;
        mp->active = true;
        mountCount++;
        
        EarlyTerm::Print("[Mount] Mounted ");
        EarlyTerm::Print(fs_type);
        EarlyTerm::Print(" at ");
        EarlyTerm::Print(path);
        EarlyTerm::Print("\n");
        
        return true;
    }
    
    bool Unmount(const char* path) {
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (mounts[i].active && kstrcmp(mounts[i].path, path) == 0) {
                mounts[i].active = false;
                mounts[i].fs = nullptr;
                mountCount--;
                
                EarlyTerm::Print("[Mount] Unmounted ");
                EarlyTerm::Print(path);
                EarlyTerm::Print("\n");
                return true;
            }
        }
        return false;
    }
    
    MountPoint* FindMount(const char* path, const char** remaining_path) {
        MountPoint* best = nullptr;
        int best_len = 0;
        
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (!mounts[i].active) continue;
            
            int len = kstrlen(mounts[i].path);
            
            // Check if path starts with this mount point
            bool match = true;
            for (int j = 0; j < len; j++) {
                if (path[j] != mounts[i].path[j]) {
                    match = false;
                    break;
                }
            }
            
            // Verify it's a proper prefix (followed by / or end)
            if (match && len > best_len) {
                if (path[len] == '/' || path[len] == 0 || len == 1) {
                    best = &mounts[i];
                    best_len = len;
                }
            }
        }
        
        if (best && remaining_path) {
            const char* rem = path + best_len;
            if (*rem == '/') rem++;
            *remaining_path = rem;
        }
        
        return best;
    }
    
    MountPoint* GetMount(int index) {
        if (index < 0 || index >= MAX_MOUNT_POINTS) return nullptr;
        return mounts[index].active ? &mounts[index] : nullptr;
    }
    
    int GetMountCount() {
        return mountCount;
    }
    
    void ListMounts() {
        EarlyTerm::Print("--- Mount Points ---\n");
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (mounts[i].active) {
                EarlyTerm::Print("  ");
                EarlyTerm::Print(mounts[i].device[0] ? mounts[i].device : "(none)");
                EarlyTerm::Print(" on ");
                EarlyTerm::Print(mounts[i].path);
                EarlyTerm::Print(" type ");
                EarlyTerm::Print(mounts[i].fs_type);
                EarlyTerm::Print("\n");
            }
        }
    }
    
    bool AddMount(const char* path, IFileSystem* fs) {
        if (!path || !fs) return false;
        
        // Find free slot
        int slot = -1;
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (!mounts[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot < 0) return false;
        
        // Fill mount point
        MountPoint* mp = &mounts[slot];
        kmemset(mp, 0, sizeof(MountPoint));
        
        // Copy path
        int len = kstrlen(path);
        if (len >= MAX_PATH_LENGTH) len = MAX_PATH_LENGTH - 1;
        kmemcpy(mp->path, path, len);
        mp->path[len] = 0;
        
        // Copy fs type name
        if (fs->name) {
            len = kstrlen(fs->name);
            if (len >= MAX_FSTYPE_LENGTH) len = MAX_FSTYPE_LENGTH - 1;
            kmemcpy(mp->fs_type, fs->name, len);
            mp->fs_type[len] = 0;
        }
        
        mp->fs = fs;
        mp->active = true;
        mountCount++;
        
        return true;
    }
}
