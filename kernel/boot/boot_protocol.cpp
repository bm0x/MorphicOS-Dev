// Boot Protocol Implementation
// Parses boot information from various bootloaders

#include "boot_protocol.h"
#include "../utils/std.h"

namespace Boot {
    static BootInfo bootInfo;
    
    BootInfo* GetBootInfo() {
        return &bootInfo;
    }
    
    void ParseUEFI(void* system_table, void* image_handle) {
        bootInfo.magic = BOOT_MAGIC;
        bootInfo.protocol = BootProtocol::UEFI;
        
        // UEFI parsing would extract:
        // - EFI_MEMORY_DESCRIPTOR array from GetMemoryMap()
        // - GOP framebuffer info
        // - ACPI RSDP from configuration tables
        // - Command line from loaded image info
        
        bootInfo.arch_data = system_table;
        (void)image_handle;
    }
    
    void ParseDTB(void* dtb_ptr) {
        bootInfo.magic = BOOT_MAGIC;
        bootInfo.protocol = BootProtocol::DTB;
        bootInfo.dtb = dtb_ptr;
        
        // DTB parsing would extract:
        // - /memory node for memory regions
        // - /chosen node for command line and initrd
        // - Device addresses for UART, interrupt controller, etc.
        
        // DTB magic check: 0xD00DFEED (big-endian)
        if (dtb_ptr) {
            uint32_t magic = *((uint32_t*)dtb_ptr);
            // Swap bytes for big-endian
            magic = ((magic >> 24) & 0xFF) | 
                    ((magic >> 8) & 0xFF00) |
                    ((magic << 8) & 0xFF0000) |
                    ((magic << 24) & 0xFF000000);
            
            if (magic != 0xD00DFEED) {
                bootInfo.dtb = nullptr;  // Invalid DTB
            }
        }
    }
    
    const char* GetCmdlineArg(const char* key) {
        if (!key || !bootInfo.cmdline[0]) return nullptr;
        
        const char* p = bootInfo.cmdline;
        int keyLen = kstrlen(key);
        
        while (*p) {
            // Skip whitespace
            while (*p == ' ') p++;
            if (!*p) break;
            
            // Check for key match
            bool match = true;
            for (int i = 0; i < keyLen && *p; i++, p++) {
                if (*p != key[i]) {
                    match = false;
                }
            }
            
            if (match && *p == '=') {
                return p + 1;  // Return value after '='
            }
            
            // Skip to next argument
            while (*p && *p != ' ') p++;
        }
        
        return nullptr;
    }
    
    MemoryRegion* FindMemoryRegion(MemoryType type, int index) {
        int found = 0;
        for (uint32_t i = 0; i < bootInfo.memory_map_count; i++) {
            if (bootInfo.memory_map[i].type == type) {
                if (found == index) {
                    return &bootInfo.memory_map[i];
                }
                found++;
            }
        }
        return nullptr;
    }
    
    uint64_t GetUsableMemory() {
        if (bootInfo.total_memory > 0) {
            return bootInfo.total_memory;
        }
        
        uint64_t total = 0;
        for (uint32_t i = 0; i < bootInfo.memory_map_count; i++) {
            if (bootInfo.memory_map[i].type == MemoryType::USABLE) {
                total += bootInfo.memory_map[i].length;
            }
        }
        
        bootInfo.total_memory = total;
        return total;
    }
    
    bool GetInitRD(void** base, uint64_t* size) {
        if (!bootInfo.initrd_base || bootInfo.initrd_size == 0) {
            return false;
        }
        
        if (base) *base = bootInfo.initrd_base;
        if (size) *size = bootInfo.initrd_size;
        
        return true;
    }
}
