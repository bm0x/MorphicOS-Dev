// MCL Module System Implementation
// Dynamic command plugin loader

#include "mcl_module.h"
#include "../mm/user_heap.h"
#include "../fs/vfs.h"
#include "../hal/video/early_term.h"
#include "../utils/std.h"

namespace MCLModule {
    // Loaded modules
    static LoadedModule modules[MAX_MODULES];
    static int moduleCount = 0;
    
    // Registered handlers (for built-in extensions)
    struct HandlerEntry {
        char name[32];
        ModuleHandlerFn handler;
        bool active;
    };
    static HandlerEntry handlers[MAX_MODULE_CMDS];
    static int handlerCount = 0;
    
    void Init() {
        moduleCount = 0;
        handlerCount = 0;
        
        for (int i = 0; i < MAX_MODULES; i++) {
            modules[i].active = false;
        }
        
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            handlers[i].active = false;
        }
        
        EarlyTerm::Print("[MCLModule] System initialized.\n");
    }
    
    bool Load(const char* path) {
        if (moduleCount >= MAX_MODULES) {
            EarlyTerm::Print("[MCLModule] Max modules reached\n");
            return false;
        }
        
        // Open module file
        VFSNode* node = VFS::Open(path);
        if (!node) {
            EarlyTerm::Print("[MCLModule] File not found: ");
            EarlyTerm::Print(path);
            EarlyTerm::Print("\n");
            return false;
        }
        
        // Read header
        ModuleHeader header;
        if (VFS::Read(node, 0, sizeof(header), (uint8_t*)&header) != sizeof(header)) {
            EarlyTerm::Print("[MCLModule] Failed to read header\n");
            return false;
        }
        
        // Validate magic
        if (header.magic != MODULE_MAGIC) {
            EarlyTerm::Print("[MCLModule] Invalid module magic\n");
            return false;
        }
        
        // Allocate memory for module
        size_t totalSize = node->size;
        void* moduleMem = UserHeap::Allocate(totalSize, MEM_PRIORITY_NORMAL);
        if (!moduleMem) {
            EarlyTerm::Print("[MCLModule] Out of memory\n");
            return false;
        }
        
        // Load entire module
        VFS::Read(node, 0, totalSize, (uint8_t*)moduleMem);
        
        // Setup module entry
        int slot = -1;
        for (int i = 0; i < MAX_MODULES; i++) {
            if (!modules[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot < 0) {
            UserHeap::Free(moduleMem);
            return false;
        }
        
        LoadedModule* mod = &modules[slot];
        kmemcpy(mod->name, header.name, 32);
        mod->base = moduleMem;
        mod->size = totalSize;
        mod->command_count = header.entry_count;
        mod->active = true;
        
        // Parse command entries
        if (header.entry_count > 0) {
            size_t cmdOffset = sizeof(ModuleHeader);
            mod->commands = (ModuleCommand*)((uint8_t*)moduleMem + cmdOffset);
        } else {
            mod->commands = nullptr;
        }
        
        moduleCount++;
        
        EarlyTerm::Print("[MCLModule] Loaded: ");
        EarlyTerm::Print(mod->name);
        EarlyTerm::Print(" (");
        EarlyTerm::PrintDec(header.entry_count);
        EarlyTerm::Print(" commands)\n");
        
        return true;
    }
    
    bool Unload(const char* name) {
        for (int i = 0; i < MAX_MODULES; i++) {
            if (modules[i].active && kstrcmp(modules[i].name, name) == 0) {
                UserHeap::Free(modules[i].base);
                modules[i].active = false;
                moduleCount--;
                
                EarlyTerm::Print("[MCLModule] Unloaded: ");
                EarlyTerm::Print(name);
                EarlyTerm::Print("\n");
                return true;
            }
        }
        return false;
    }
    
    ModuleHandlerFn GetHandler(const char* action) {
        // Check registered handlers first
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            if (handlers[i].active && kstrcmp(handlers[i].name, action) == 0) {
                return handlers[i].handler;
            }
        }
        
        // Check loaded modules
        // TODO: For binary modules, would need to resolve function pointers
        
        return nullptr;
    }
    
    bool IsModuleCommand(const char* action) {
        return GetHandler(action) != nullptr;
    }
    
    int Execute(const char* action, const MCLCommand* cmd) {
        ModuleHandlerFn handler = GetHandler(action);
        if (handler) {
            return handler(cmd);
        }
        return -1;
    }
    
    void ListModules() {
        EarlyTerm::Print("=== Loaded Modules ===\n");
        
        if (moduleCount == 0) {
            EarlyTerm::Print("  (none)\n");
            return;
        }
        
        for (int i = 0; i < MAX_MODULES; i++) {
            if (modules[i].active) {
                EarlyTerm::Print("  ");
                EarlyTerm::Print(modules[i].name);
                EarlyTerm::Print(" - ");
                EarlyTerm::PrintDec(modules[i].command_count);
                EarlyTerm::Print(" commands, ");
                EarlyTerm::PrintDec(modules[i].size / 1024);
                EarlyTerm::Print(" KB\n");
            }
        }
        
        EarlyTerm::Print("\n=== Registered Handlers ===\n");
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            if (handlers[i].active) {
                EarlyTerm::Print("  ");
                EarlyTerm::Print(handlers[i].name);
                EarlyTerm::Print("\n");
            }
        }
    }
    
    LoadedModule* GetModule(const char* name) {
        for (int i = 0; i < MAX_MODULES; i++) {
            if (modules[i].active && kstrcmp(modules[i].name, name) == 0) {
                return &modules[i];
            }
        }
        return nullptr;
    }
    
    int GetLoadedCount() {
        return moduleCount;
    }
    
    bool RegisterHandler(const char* name, ModuleHandlerFn handler) {
        if (!name || !handler) return false;
        
        // Check if already registered
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            if (handlers[i].active && kstrcmp(handlers[i].name, name) == 0) {
                return false;  // Already exists
            }
        }
        
        // Find free slot
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            if (!handlers[i].active) {
                int j = 0;
                while (name[j] && j < 31) {
                    handlers[i].name[j] = name[j];
                    j++;
                }
                handlers[i].name[j] = 0;
                handlers[i].handler = handler;
                handlers[i].active = true;
                handlerCount++;
                return true;
            }
        }
        
        return false;
    }
    
    bool UnregisterHandler(const char* name) {
        for (int i = 0; i < MAX_MODULE_CMDS; i++) {
            if (handlers[i].active && kstrcmp(handlers[i].name, name) == 0) {
                handlers[i].active = false;
                handlerCount--;
                return true;
            }
        }
        return false;
    }
}
