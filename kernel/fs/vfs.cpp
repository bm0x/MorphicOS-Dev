#include "vfs.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

namespace VFS {
    static VFSNode* rootNode = nullptr;
    
    void Init() {
        // Root will be set by InitRD::Init()
        rootNode = nullptr;
        EarlyTerm::Print("[VFS] Initialized.\n");
    }
    
    VFSNode* GetRoot() {
        return rootNode;
    }
    
    // Internal: Set root (called by filesystem drivers)
    void SetRoot(VFSNode* root) {
        rootNode = root;
    }
    
    // Internal: Find child node by name
    static VFSNode* FindChild(VFSNode* parent, const char* name) {
        if (!parent || parent->type != NodeType::DIRECTORY) return nullptr;
        
        VFSNode* child = parent->firstChild;
        while (child) {
            if (kstrcmp(child->name, name) == 0) {
                return child;
            }
            child = child->nextSibling;
        }
        return nullptr;
    }
    
    // Internal: Parse path and find node
    VFSNode* Open(const char* path) {
        if (!path || !rootNode) return nullptr;
        
        // Handle root
        if (kstrcmp(path, "/") == 0) {
            return rootNode;
        }
        
        // Skip leading slash
        if (*path == '/') path++;
        
        VFSNode* current = rootNode;
        char component[64];
        int compLen = 0;
        
        while (*path) {
            if (*path == '/') {
                if (compLen > 0) {
                    component[compLen] = 0;
                    current = FindChild(current, component);
                    if (!current) return nullptr;
                    compLen = 0;
                }
            } else {
                if (compLen < 63) {
                    component[compLen++] = *path;
                }
            }
            path++;
        }
        
        // Handle last component
        if (compLen > 0) {
            component[compLen] = 0;
            current = FindChild(current, component);
        }
        
        return current;
    }
    
    uint32_t Read(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !buffer) return 0;
        if (node->type != NodeType::FILE) return 0;
        
        // Use node's read function if available
        if (node->read) {
            return node->read(node, offset, size, buffer);
        }
        
        // Default: direct memory read (for InitRD)
        if (!node->data) return 0;
        if (offset >= node->size) return 0;
        
        uint32_t available = node->size - offset;
        uint32_t toRead = (size < available) ? size : available;
        
        kmemcpy(buffer, node->data + offset, toRead);
        return toRead;
    }
    
    uint32_t Write(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !buffer) return 0;
        if (node->type != NodeType::FILE) return 0;
        
        // Use node's write function if available
        if (node->write) {
            return node->write(node, offset, size, buffer);
        }
        
        // Default: no write support for basic InitRD
        return 0;
    }
    
    void Close(VFSNode* node) {
        // No-op for InitRD (no state to clean)
        (void)node;
    }
    
    VFSNode* CreateFile(const char* path) {
        if (!path || !rootNode) return nullptr;
        
        // For now, only support creating in root directory
        // Skip leading slash
        if (*path == '/') path++;
        
        // Check if it contains more slashes (subdirectory)
        for (const char* p = path; *p; p++) {
            if (*p == '/') {
                // Subdirectory creation not supported yet
                return nullptr;
            }
        }
        
        // Check if already exists
        if (FindChild(rootNode, path)) {
            return nullptr; // Already exists
        }
        
        // Create new node
        VFSNode* newNode = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!newNode) return nullptr;
        
        kmemset(newNode, 0, sizeof(VFSNode));
        
        // Copy name (safely)
        int i = 0;
        while (path[i] && i < 63) {
            newNode->name[i] = path[i];
            i++;
        }
        newNode->name[i] = 0;
        
        newNode->type = NodeType::FILE;
        newNode->size = 0;
        newNode->data = nullptr;
        newNode->parent = rootNode;
        newNode->read = nullptr;
        newNode->write = nullptr;
        
        // Add to root's children
        newNode->nextSibling = rootNode->firstChild;
        rootNode->firstChild = newNode;
        
        return newNode;
    }
    
    VFSNode** ListDir(VFSNode* dir, uint32_t* outCount) {
        if (!dir || dir->type != NodeType::DIRECTORY || !outCount) {
            if (outCount) *outCount = 0;
            return nullptr;
        }
        
        // Count children
        uint32_t count = 0;
        VFSNode* child = dir->firstChild;
        while (child) {
            count++;
            child = child->nextSibling;
        }
        
        *outCount = count;
        if (count == 0) return nullptr;
        
        // Allocate array
        VFSNode** result = (VFSNode**)kmalloc(count * sizeof(VFSNode*));
        if (!result) {
            *outCount = 0;
            return nullptr;
        }
        
        // Fill array
        child = dir->firstChild;
        for (uint32_t i = 0; i < count && child; i++) {
            result[i] = child;
            child = child->nextSibling;
        }
        
        return result;
    }
    
    VFSNode* Mkdir(const char* path) {
        if (!path || !rootNode) return nullptr;
        
        // Skip leading slash
        if (*path == '/') path++;
        
        // Check if it contains more slashes
        for (const char* p = path; *p; p++) {
            if (*p == '/') return nullptr;
        }
        
        // Check if already exists
        if (FindChild(rootNode, path)) return nullptr;
        
        // Create new directory node
        VFSNode* newNode = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!newNode) return nullptr;
        
        kmemset(newNode, 0, sizeof(VFSNode));
        
        int i = 0;
        while (path[i] && i < 63) {
            newNode->name[i] = path[i];
            i++;
        }
        newNode->name[i] = 0;
        
        newNode->type = NodeType::DIRECTORY;
        newNode->size = 0;
        newNode->data = nullptr;
        newNode->parent = rootNode;
        newNode->firstChild = nullptr;
        
        newNode->nextSibling = rootNode->firstChild;
        rootNode->firstChild = newNode;
        
        return newNode;
    }
    
    bool Delete(const char* path) {
        VFSNode* node = Open(path);
        if (!node || node == rootNode) return false;
        if (node->type != NodeType::FILE) return false;
        
        // Remove from parent's child list
        VFSNode* parent = node->parent;
        if (!parent) return false;
        
        if (parent->firstChild == node) {
            parent->firstChild = node->nextSibling;
        } else {
            VFSNode* prev = parent->firstChild;
            while (prev && prev->nextSibling != node) {
                prev = prev->nextSibling;
            }
            if (prev) {
                prev->nextSibling = node->nextSibling;
            }
        }
        
        // Free data and node
        if (node->data) kfree(node->data);
        kfree(node);
        
        return true;
    }
    
    bool Rmdir(const char* path) {
        VFSNode* node = Open(path);
        if (!node || node == rootNode) return false;
        if (node->type != NodeType::DIRECTORY) return false;
        if (node->firstChild != nullptr) return false; // Not empty
        
        // Remove from parent's child list
        VFSNode* parent = node->parent;
        if (!parent) return false;
        
        if (parent->firstChild == node) {
            parent->firstChild = node->nextSibling;
        } else {
            VFSNode* prev = parent->firstChild;
            while (prev && prev->nextSibling != node) {
                prev = prev->nextSibling;
            }
            if (prev) {
                prev->nextSibling = node->nextSibling;
            }
        }
        
        kfree(node);
        return true;
    }
    
    bool Rename(const char* oldpath, const char* newpath) {
        VFSNode* node = Open(oldpath);
        if (!node || node == rootNode) return false;
        
        // Just rename in place (same directory)
        if (*newpath == '/') newpath++;
        
        int i = 0;
        while (newpath[i] && i < 63) {
            node->name[i] = newpath[i];
            i++;
        }
        node->name[i] = 0;
        
        return true;
    }
    
    bool Stat(const char* path, FileStat* stat) {
        VFSNode* node = Open(path);
        if (!node || !stat) return false;
        
        int i = 0;
        while (node->name[i] && i < 63) {
            stat->name[i] = node->name[i];
            i++;
        }
        stat->name[i] = 0;
        
        stat->type = node->type;
        stat->size = node->size;
        stat->created = 0;
        stat->modified = 0;
        stat->accessed = 0;
        stat->mode = 0755;
        
        return true;
    }
}

// Global setter for filesystem drivers
extern "C" void vfs_set_root(VFSNode* root) {
    VFS::SetRoot(root);
}

