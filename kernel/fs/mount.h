#pragma once

// Mount Point System
// Manages filesystem mount points for VFS

#include <stdint.h>
#include <stddef.h>


#define MAX_MOUNT_POINTS 16
#define MAX_PATH_LENGTH 64
#define MAX_DEVICE_LENGTH 32
#define MAX_FSTYPE_LENGTH 16

// Mount flags
#define MOUNT_RDONLY     (1 << 0)  // Read-only
#define MOUNT_NOEXEC     (1 << 1)  // No execution
#define MOUNT_NOSUID     (1 << 2)  // No setuid
#define MOUNT_NODEV      (1 << 3)  // No device files
#define MOUNT_SYNC       (1 << 4)  // Synchronous I/O

// Forward declaration for filesystem interface
struct IFileSystem;

// Mount point structure
struct MountPoint {
    char path[MAX_PATH_LENGTH];          // Mount path (e.g., "/", "/data")
    char device[MAX_DEVICE_LENGTH];      // Device name (e.g., "sda1", "mmcblk0p2")
    char fs_type[MAX_FSTYPE_LENGTH];     // Filesystem type (e.g., "fat32", "ext4")
    uint32_t flags;                      // Mount flags
    IFileSystem* fs;                     // Filesystem driver
    bool active;                         // Is this mount point active?
};

// Filesystem interface (VFS drivers implement this)
struct IFileSystem {
    const char* name;                    // e.g., "fat32", "ext4", "initrd"
    
    // Operations
    int (*read)(IFileSystem* self, const char* path, void* buf, size_t size, size_t offset);
    int (*write)(IFileSystem* self, const char* path, const void* buf, size_t size, size_t offset);
    int (*create)(IFileSystem* self, const char* path, uint32_t mode);
    int (*remove)(IFileSystem* self, const char* path);
    int (*rename)(IFileSystem* self, const char* oldpath, const char* newpath);
    int (*mkdir)(IFileSystem* self, const char* path);
    int (*rmdir)(IFileSystem* self, const char* path);
    int (*stat)(IFileSystem* self, const char* path, void* stat_buf);
    int (*readdir)(IFileSystem* self, const char* path, void* entries, int max_entries);
    
    // Filesystem-specific data
    void* private_data;
};

namespace Mount {
    // Initialize mount system
    void Init();
    
    // Mount a filesystem
    // device: device name (can be nullptr for virtual fs like initrd)
    // path: mount point path
    // fs_type: filesystem type name
    // flags: mount flags
    bool Mount(const char* device, const char* path, const char* fs_type, uint32_t flags);
    
    // Unmount a filesystem
    bool Unmount(const char* path);
    
    // Find mount point for a given path
    // Returns the mount point and sets remaining_path to the path relative to mount
    MountPoint* FindMount(const char* path, const char** remaining_path);
    
    // Get mount point by index
    MountPoint* GetMount(int index);
    
    // Get number of active mounts
    int GetMountCount();
    
    // List all mount points (debug)
    void ListMounts();
    
    // Register a filesystem driver
    bool RegisterFS(const char* name, IFileSystem* fs_template);
    
    // Get filesystem driver by name
    IFileSystem* GetFSByName(const char* name);
    
    // Add a pre-mounted filesystem directly (used by MountManager)
    bool AddMount(const char* path, IFileSystem* fs);
}
