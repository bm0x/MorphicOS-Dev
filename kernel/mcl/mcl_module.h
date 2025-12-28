#pragma once

// MCL Dynamic Module System
// Runtime-loadable command plugins

#include <stdint.h>
#include <stddef.h>
#include "../mcl/mcl_parser.h"


#define MODULE_MAGIC    0x4D4D4F44  // "MMOD"
#define MODULE_VERSION  1
#define MAX_MODULES     16
#define MAX_MODULE_CMDS 32

// Module header (in .mod file)
struct ModuleHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t checksum;
    char name[32];
    char description[64];
    uint32_t entry_count;
    uint32_t code_offset;
    uint32_t code_size;
};

// Command entry in module
struct ModuleCommand {
    char name[32];          // Command name (e.g., "ping")
    char description[64];   // Help text
    uint32_t handler_offset; // Offset in code section
};

// Loaded module instance
struct LoadedModule {
    char name[32];
    void* base;             // Memory base
    size_t size;            // Total size
    ModuleCommand* commands;
    int command_count;
    bool active;
};

// Module handler function type
typedef int (*ModuleHandlerFn)(const MCLCommand* cmd);

namespace MCLModule {
    // Initialize module system
    void Init();
    
    // Load module from VFS path
    bool Load(const char* path);
    
    // Unload module by name
    bool Unload(const char* name);
    
    // Find handler for command
    ModuleHandlerFn GetHandler(const char* action);
    
    // Check if command is from a module
    bool IsModuleCommand(const char* action);
    
    // Execute module command
    int Execute(const char* action, const MCLCommand* cmd);
    
    // List loaded modules
    void ListModules();
    
    // Get module info
    LoadedModule* GetModule(const char* name);
    
    // Get all loaded modules
    int GetLoadedCount();
    
    // Register internal command handler
    bool RegisterHandler(const char* name, ModuleHandlerFn handler);
    
    // Unregister handler
    bool UnregisterHandler(const char* name);
}
