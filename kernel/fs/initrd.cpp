#include "initrd.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

// External VFS setter
extern "C" void vfs_set_root(VFSNode* root);

namespace InitRD {
    static VFSNode* root = nullptr;
    
    // Helper: Create a file node
    static VFSNode* CreateFileNode(const char* name, const char* content) {
        VFSNode* node = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!node) return nullptr;
        
        kmemset(node, 0, sizeof(VFSNode));
        
        // Copy name
        int i = 0;
        while (name[i] && i < 63) {
            node->name[i] = name[i];
            i++;
        }
        node->name[i] = 0;
        
        node->type = NodeType::FILE;
        
        // Copy content if provided
        if (content) {
            node->size = kstrlen(content);
            node->data = (uint8_t*)kmalloc(node->size + 1);
            if (node->data) {
                kmemcpy(node->data, content, node->size + 1);
            }
        } else {
            node->size = 0;
            node->data = nullptr;
        }
        
        return node;
    }
    
    // Helper: Create directory node
    static VFSNode* CreateDirNode(const char* name) {
        VFSNode* node = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!node) return nullptr;
        
        kmemset(node, 0, sizeof(VFSNode));
        
        // Copy name
        int i = 0;
        while (name[i] && i < 63) {
            node->name[i] = name[i];
            i++;
        }
        node->name[i] = 0;
        
        node->type = NodeType::DIRECTORY;
        node->size = 0;
        node->data = nullptr;
        
        return node;
    }
    
    // Helper: Add child to directory
    static void AddChild(VFSNode* parent, VFSNode* child) {
        if (!parent || !child) return;
        child->parent = parent;
        child->nextSibling = parent->firstChild;
        parent->firstChild = child;
    }
    
    void Init() {
        // Create root directory
        root = CreateDirNode("/");
        if (!root) {
            EarlyTerm::Print("[InitRD] ERROR: Cannot allocate root!\n");
            return;
        }
        
        // Create /etc directory for config files
        VFSNode* etcDir = CreateDirNode("etc");
        if (etcDir) AddChild(root, etcDir);
        
        // Create boot.cfg config file
        VFSNode* bootcfg = CreateFileNode("boot.cfg",
            "# Morphic OS Boot Configuration\n"
            "showlogo=1\n"
            "verbose=1\n"
            "sound=0\n"
            "timeout=0\n"
            "loglevel=3\n");
        if (bootcfg && etcDir) AddChild(etcDir, bootcfg);
        
        // Create default files in root
        VFSNode* hello = CreateFileNode("hello.txt", 
            "Hello from Morphic OS!\n"
            "This file is stored in InitRD (RAM).\n"
            "Welcome to Phase Swift Graphics!\n");
        
        VFSNode* readme = CreateFileNode("readme.txt",
            "Morphic OS - MIT License\n"
            "A lightweight x86_64 operating system.\n"
            "Commands: help, info, ls, cat, logotest, mousetest\n");
        
        if (hello) AddChild(root, hello);
        if (readme) AddChild(root, readme);
        
        // Set VFS root
        vfs_set_root(root);
        
        EarlyTerm::Print("[InitRD] Mounted at / with ");
        uint32_t count = 0;
        VFSNode* child = root->firstChild;
        while (child) { count++; child = child->nextSibling; }
        EarlyTerm::PrintDec(count);
        EarlyTerm::Print(" files.\n");
    }
    
    VFSNode* GetRoot() {
        return root;
    }
}
