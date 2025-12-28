#pragma once

// Boot Protocol Abstraction
// Unified boot information for UEFI (x86) and DTB (ARM)

#include <stdint.h>

// Boot protocol types
enum class BootProtocol {
    UNKNOWN = 0,
    UEFI,           // x86 UEFI from GOP/EFI_HANDLE
    MULTIBOOT,      // Legacy GRUB multiboot
    DTB,            // ARM Device Tree Blob
    LK,             // Little Kernel (Android bootloader)
    UBOOT           // U-Boot (ARM)
};

// Memory region types (unified)
enum class MemoryType {
    USABLE = 0,         // Free RAM
    RESERVED,           // System reserved
    ACPI_RECLAIMABLE,   // ACPI tables (can free after parsing)
    ACPI_NVS,           // ACPI non-volatile storage
    BAD,                // Faulty memory
    BOOTLOADER,         // Bootloader data
    KERNEL,             // Kernel code/data
    FRAMEBUFFER,        // Graphics memory
    MMIO                // Memory-mapped I/O
};

// Memory region descriptor
struct MemoryRegion {
    uint64_t base;
    uint64_t length;
    MemoryType type;
    uint32_t attributes;
};

// Framebuffer information (unified)
struct FramebufferInfo {
    uint64_t baseAddress;
    uint32_t width;
    uint32_t height;
    uint32_t pixelsPerScanLine;
    uint32_t bitsPerPixel;
    uint32_t redMask;
    uint32_t greenMask;
    uint32_t blueMask;
};

// Unified boot information structure
struct BootInfo {
    uint64_t magic;                 // 0x4D4F5250484943 = "MORPHIC"
    BootProtocol protocol;          // Boot protocol used
    
    // Memory map
    MemoryRegion* memory_map;
    uint32_t memory_map_count;
    uint64_t total_memory;          // Total usable RAM in bytes
    
    // Framebuffer
    FramebufferInfo framebuffer;
    bool has_framebuffer;
    
    // Kernel command line
    char cmdline[256];
    
    // InitRD / Ramdisk
    void* initrd_base;
    uint64_t initrd_size;
    
    // Architecture-specific data
    void* arch_data;                // Pointer to ACPI tables, DTB, etc.
    uint64_t arch_data_size;
    
    // RSDP for ACPI (x86)
    void* rsdp;
    
    // Device Tree Blob (ARM)
    void* dtb;
    
    // Boot timestamp (if available)
    uint64_t boot_time;
};

#define BOOT_MAGIC 0x4D4F5250484943ULL

namespace Boot {
    // Get the current boot info
    BootInfo* GetBootInfo();
    
    // Parse UEFI boot data
    void ParseUEFI(void* system_table, void* image_handle);
    
    // Parse Device Tree Blob
    void ParseDTB(void* dtb_ptr);
    
    // Parse command line arguments
    const char* GetCmdlineArg(const char* key);
    
    // Find memory region by type
    MemoryRegion* FindMemoryRegion(MemoryType type, int index);
    
    // Calculate total usable memory
    uint64_t GetUsableMemory();
    
    // Get the initrd if present
    bool GetInitRD(void** base, uint64_t* size);
}
