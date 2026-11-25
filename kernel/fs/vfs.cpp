#include "vfs.h"
#include "mount.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

namespace VFS {
    static VFSNode* rootNode = nullptr;

    static void CopyStrSafe(char* dst, size_t maxLen, const char* src) {
        if (!dst || maxLen == 0) return;
        size_t i = 0;
        if (src) {
            while (src[i] && i < maxLen - 1) {
                dst[i] = src[i];
                i++;
            }
        }
        dst[i] = 0;
    }

    static void ExtractLeafName(const char* path, char* out, size_t maxLen) {
        if (!path || !out || maxLen == 0) return;
        const char* last = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/' && *(p + 1) != '\0') {
                last = p + 1;
            }
        }
        if (*last == '/') last++;
        if (*last == '\0') {
            CopyStrSafe(out, maxLen, "/");
            return;
        }
        size_t i = 0;
        while (last[i] && last[i] != '/' && i < maxLen - 1) {
            out[i] = last[i];
            i++;
        }
        out[i] = 0;
    }

    static uint32_t RamFileWrite(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !buffer || size == 0) return 0;
        uint64_t endOffset = (uint64_t)offset + size;
        if (endOffset > 32 * 1024 * 1024) return 0; // Cap RAM file growth at 32 MiB

        if (endOffset > node->size || !node->data) {
            uint8_t* newBuf = (uint8_t*)kmalloc((size_t)endOffset + 1);
            if (!newBuf) return 0;
            kmemset(newBuf, 0, (size_t)endOffset + 1);
            if (node->data && node->size > 0) {
                kmemcpy(newBuf, node->data, node->size);
                kfree(node->data);
            }
            node->data = newBuf;
            node->size = (uint32_t)endOffset;
        }
        kmemcpy(node->data + offset, buffer, size);
        return size;
    }

    static uint32_t MountedNodeRead(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !node->mounted_fs || !node->mounted_fs->read || !buffer) return 0;
        int r = node->mounted_fs->read(node->mounted_fs, node->mount_rel_path, buffer, size, offset);
        return (r > 0) ? (uint32_t)r : 0;
    }

    static uint32_t MountedNodeWrite(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !node->mounted_fs || !node->mounted_fs->write || !buffer) return 0;
        int w = node->mounted_fs->write(node->mounted_fs, node->mount_rel_path, buffer, size, offset);
        if (w > 0 && offset + (uint32_t)w > node->size) {
            node->size = offset + (uint32_t)w;
        }
        return (w > 0) ? (uint32_t)w : 0;
    }

    static VFSNode* CreateMountedTransientNode(IFileSystem* fs, const char* relPath, const FileStat& st, const char* fullPath) {
        VFSNode* node = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!node) return nullptr;
        kmemset(node, 0, sizeof(VFSNode));

        if (st.name[0] && kstrcmp(st.name, "/") != 0) {
            CopyStrSafe(node->name, sizeof(node->name), st.name);
        } else {
            ExtractLeafName(fullPath, node->name, sizeof(node->name));
        }

        node->type = st.type;
        node->size = st.size;
        node->mounted_fs = fs;
        CopyStrSafe(node->mount_rel_path, sizeof(node->mount_rel_path), (relPath && relPath[0]) ? relPath : "/");
        node->is_transient = true;
        node->read = MountedNodeRead;
        node->write = MountedNodeWrite;
        return node;
    }

    void Init() {
        rootNode = nullptr;
        Mount::Init();
        EarlyTerm::Print("[VFS] Initialized.\n");
    }

    VFSNode* GetRoot() {
        return rootNode;
    }

    void SetRoot(VFSNode* root) {
        rootNode = root;
    }

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

    static VFSNode* ResolveRamParent(const char* path, char* outLeaf, size_t maxLeafLen) {
        if (!path || !rootNode || !outLeaf) return nullptr;
        if (*path == '/') path++;
        if (*path == '\0') return nullptr;

        VFSNode* current = rootNode;
        while (*path) {
            const char* slash = path;
            while (*slash && *slash != '/') slash++;
            size_t len = (size_t)(slash - path);
            if (len == 0) {
                path++;
                continue;
            }

            // Check if any non-slash characters remain after slash
            const char* after = slash;
            while (*after == '/') after++;
            if (*after == '\0') {
                // Last component!
                if (len >= maxLeafLen) len = maxLeafLen - 1;
                kmemcpy(outLeaf, path, len);
                outLeaf[len] = 0;
                return current;
            }

            char comp[64];
            if (len >= sizeof(comp)) len = sizeof(comp) - 1;
            kmemcpy(comp, path, len);
            comp[len] = 0;

            current = FindChild(current, comp);
            if (!current || current->type != NodeType::DIRECTORY) return nullptr;
            path = after;
        }
        return nullptr;
    }

    void EnsureMountStub(const char* mountPath) {
        if (!mountPath || !rootNode) return;
        const char* name = mountPath;
        if (*name == '/') name++;
        if (*name == '\0') return;
        for (const char* p = name; *p; p++) {
            if (*p == '/') return; // Only single-level root mount stubs needed
        }
        if (FindChild(rootNode, name)) return;

        VFSNode* stub = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!stub) return;
        kmemset(stub, 0, sizeof(VFSNode));
        CopyStrSafe(stub->name, sizeof(stub->name), name);
        stub->type = NodeType::DIRECTORY;
        stub->parent = rootNode;
        stub->nextSibling = rootNode->firstChild;
        rootNode->firstChild = stub;
    }

    VFSNode* Open(const char* path) {
        if (!path) return nullptr;

        // 1. Route through Mount Point table first (e.g. /data -> FAT32)
        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs && mp->fs->stat) {
            FileStat st = {};
            const char* sub = (relPath && relPath[0]) ? relPath : "/";
            if (mp->fs->stat(mp->fs, sub, &st) == 0) {
                return CreateMountedTransientNode(mp->fs, sub, st, path);
            }
            return nullptr;
        }

        // 2. Fallback to in-memory InitRD / RAM VFS tree
        if (!rootNode) return nullptr;
        if (kstrcmp(path, "/") == 0) {
            return rootNode;
        }

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

        if (compLen > 0) {
            component[compLen] = 0;
            current = FindChild(current, component);
        }

        return current;
    }

    uint32_t Read(VFSNode* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
        if (!node || !buffer) return 0;
        if (node->type != NodeType::FILE) return 0;

        if (node->read) {
            return node->read(node, offset, size, buffer);
        }

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

        if (node->write) {
            return node->write(node, offset, size, buffer);
        }

        return RamFileWrite(node, offset, size, buffer);
    }

    void Close(VFSNode* node) {
        if (node && node->is_transient) {
            kfree(node);
        }
    }

    VFSNode* CreateFile(const char* path) {
        if (!path) return nullptr;

        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs) {
            if (!mp->fs->create || !relPath || !relPath[0]) return nullptr;
            if (mp->fs->create(mp->fs, relPath, 0644) != 0) return nullptr;
            return Open(path);
        }

        char leaf[64];
        VFSNode* parent = ResolveRamParent(path, leaf, sizeof(leaf));
        if (!parent || FindChild(parent, leaf)) return nullptr;

        VFSNode* newNode = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!newNode) return nullptr;
        kmemset(newNode, 0, sizeof(VFSNode));
        CopyStrSafe(newNode->name, sizeof(newNode->name), leaf);
        newNode->type = NodeType::FILE;
        newNode->parent = parent;
        newNode->write = RamFileWrite;

        newNode->nextSibling = parent->firstChild;
        parent->firstChild = newNode;
        return newNode;
    }

    VFSNode** ListDir(VFSNode* dir, uint32_t* outCount) {
        if (!dir || dir->type != NodeType::DIRECTORY || !outCount) {
            if (outCount) *outCount = 0;
            return nullptr;
        }

        if (dir->mounted_fs && dir->mounted_fs->readdir) {
            struct RawDirEntry {
                char name[64];
                uint32_t type;
                uint32_t size;
            };
            constexpr int kMaxDirEntries = 128;
            RawDirEntry* raw = (RawDirEntry*)kmalloc(sizeof(RawDirEntry) * kMaxDirEntries);
            if (!raw) {
                *outCount = 0;
                return nullptr;
            }
            int count = dir->mounted_fs->readdir(dir->mounted_fs, dir->mount_rel_path, raw, kMaxDirEntries);
            if (count <= 0) {
                kfree(raw);
                *outCount = 0;
                return nullptr;
            }
            VFSNode** result = (VFSNode**)kmalloc((uint32_t)count * sizeof(VFSNode*));
            if (!result) {
                kfree(raw);
                *outCount = 0;
                return nullptr;
            }
            uint32_t actual = 0;
            for (int i = 0; i < count; i++) {
                VFSNode* item = (VFSNode*)kmalloc(sizeof(VFSNode));
                if (!item) continue;
                kmemset(item, 0, sizeof(VFSNode));
                CopyStrSafe(item->name, sizeof(item->name), raw[i].name);
                item->type = (raw[i].type == 1) ? NodeType::DIRECTORY : NodeType::FILE;
                item->size = raw[i].size;
                item->mounted_fs = dir->mounted_fs;
                item->is_transient = true;
                result[actual++] = item;
            }
            kfree(raw);
            *outCount = actual;
            if (actual == 0) {
                kfree(result);
                return nullptr;
            }
            return result;
        }

        uint32_t count = 0;
        VFSNode* child = dir->firstChild;
        while (child) {
            count++;
            child = child->nextSibling;
        }

        *outCount = count;
        if (count == 0) return nullptr;

        VFSNode** result = (VFSNode**)kmalloc(count * sizeof(VFSNode*));
        if (!result) {
            *outCount = 0;
            return nullptr;
        }

        child = dir->firstChild;
        for (uint32_t i = 0; i < count && child; i++) {
            result[i] = child;
            child = child->nextSibling;
        }

        return result;
    }

    void FreeDirList(VFSNode** list, uint32_t count) {
        if (!list) return;
        for (uint32_t i = 0; i < count; i++) {
            if (list[i] && list[i]->is_transient) {
                kfree(list[i]);
            }
        }
        kfree(list);
    }

    VFSNode* Mkdir(const char* path) {
        if (!path) return nullptr;

        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs) {
            if (!mp->fs->mkdir || !relPath || !relPath[0]) return nullptr;
            if (mp->fs->mkdir(mp->fs, relPath) != 0) return nullptr;
            return Open(path);
        }

        char leaf[64];
        VFSNode* parent = ResolveRamParent(path, leaf, sizeof(leaf));
        if (!parent || FindChild(parent, leaf)) return nullptr;

        VFSNode* newNode = (VFSNode*)kmalloc(sizeof(VFSNode));
        if (!newNode) return nullptr;
        kmemset(newNode, 0, sizeof(VFSNode));
        CopyStrSafe(newNode->name, sizeof(newNode->name), leaf);
        newNode->type = NodeType::DIRECTORY;
        newNode->parent = parent;

        newNode->nextSibling = parent->firstChild;
        parent->firstChild = newNode;
        return newNode;
    }

    bool Delete(const char* path) {
        if (!path) return false;

        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs) {
            if (!mp->fs->remove || !relPath || !relPath[0]) return false;
            return mp->fs->remove(mp->fs, relPath) == 0;
        }

        VFSNode* node = Open(path);
        if (!node || node == rootNode) return false;
        if (node->type != NodeType::FILE) {
            Close(node);
            return false;
        }

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

        if (node->data) kfree(node->data);
        kfree(node);
        return true;
    }

    bool Rmdir(const char* path) {
        if (!path) return false;

        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs) {
            if (!mp->fs->rmdir || !relPath || !relPath[0]) return false;
            return mp->fs->rmdir(mp->fs, relPath) == 0;
        }

        VFSNode* node = Open(path);
        if (!node || node == rootNode) return false;
        if (node->type != NodeType::DIRECTORY || node->firstChild != nullptr) {
            Close(node);
            return false;
        }

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
        if (!oldpath || !newpath) return false;

        const char* oldRel = nullptr;
        const char* newRel = nullptr;
        MountPoint* mp1 = Mount::FindMount(oldpath, &oldRel);
        MountPoint* mp2 = Mount::FindMount(newpath, &newRel);
        if (mp1 || mp2) {
            if (mp1 != mp2 || !mp1->fs || !mp1->fs->rename) return false;
            return mp1->fs->rename(mp1->fs, oldRel, newRel) == 0;
        }

        VFSNode* node = Open(oldpath);
        if (!node || node == rootNode || node->is_transient) {
            Close(node);
            return false;
        }

        char leaf[64];
        ExtractLeafName(newpath, leaf, sizeof(leaf));
        CopyStrSafe(node->name, sizeof(node->name), leaf);
        return true;
    }

    bool Stat(const char* path, FileStat* stat) {
        if (!path || !stat) return false;

        const char* relPath = nullptr;
        MountPoint* mp = Mount::FindMount(path, &relPath);
        if (mp && mp->fs && mp->fs->stat) {
            const char* sub = (relPath && relPath[0]) ? relPath : "/";
            return mp->fs->stat(mp->fs, sub, stat) == 0;
        }

        VFSNode* node = Open(path);
        if (!node) return false;

        kmemset(stat, 0, sizeof(FileStat));
        CopyStrSafe(stat->name, sizeof(stat->name), node->name);
        stat->type = node->type;
        stat->size = node->size;
        stat->mode = 0755;
        Close(node);
        return true;
    }
}

extern "C" void vfs_set_root(VFSNode* root) {
    VFS::SetRoot(root);
}
