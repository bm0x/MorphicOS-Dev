#pragma once

#include <stdint.h>

// Forward declaration
struct VFSNode;

// Node type enumeration
enum class NodeType {
    FILE,
    DIRECTORY
};

// File stats structure
struct FileStat {
    char name[64];
    NodeType type;
    uint32_t size;
    uint32_t created;   // Unix timestamp
    uint32_t modified;
    uint32_t accessed;
    uint32_t mode;      // Permissions
};

// Function pointer types for VFS operations (HAL pattern)
typedef uint32_t (*ReadFunc)(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef uint32_t (*WriteFunc)(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer);

// VFS Node structure - represents a file or directory
struct VFSNode {
    char name[64];           // Node name (filename or directory name)
    NodeType type;           // FILE or DIRECTORY
    uint32_t size;           // File size in bytes (0 for directories)
    uint8_t* data;           // Pointer to file data (InitRD uses heap)
    
    VFSNode* parent;         // Parent directory
    VFSNode* firstChild;     // First child (for directories)
    VFSNode* nextSibling;    // Next sibling in same directory
    
    // Function pointers for polymorphic FS operations
    ReadFunc read;
    WriteFunc write;
};

// VFS Namespace - Core API
namespace VFS {
    // Initialize VFS with root node
    void Init();
    
    // Get root directory
    VFSNode* GetRoot();
    
    // Open a file/directory by path (returns nullptr if not found)
    VFSNode* Open(const char* path);
    
    // Read from a file node
    uint32_t Read(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    
    // Write to a file node
    uint32_t Write(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    
    // Close a node
    void Close(VFSNode* node);
    
    // Create a new file at path
    VFSNode* CreateFile(const char* path);
    
    // Create a new directory at path
    VFSNode* Mkdir(const char* path);
    
    // Delete a file
    bool Delete(const char* path);
    
    // Delete a directory (must be empty)
    bool Rmdir(const char* path);
    
    // Rename/move a file or directory
    bool Rename(const char* oldpath, const char* newpath);
    
    // Get file stats
    bool Stat(const char* path, FileStat* stat);
    
    // List directory contents
    VFSNode** ListDir(VFSNode* dir, uint32_t* outCount);
}
