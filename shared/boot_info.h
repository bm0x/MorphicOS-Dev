#ifndef MORPHIC_BOOT_INFO_H
#define MORPHIC_BOOT_INFO_H

#include <stdint.h>

/**
 * @brief Morphic OS Boot Information Structure
 * 
 * This structure is the ONLY data passed from the UEFI Bootloader to the MiniKernel.
 * It abstracts the firmware details, providing the kernel with a clean slate.
 * 
 * Alignment: 8 bytes to ensure compatibility with 64-bit architectures.
 */

// Basic Type Definitions to avoid stdlib dependency
typedef uint64_t physical_addr_t;
typedef uint64_t virtual_addr_t;

// Memory Map Entry Type (Simplified from EFI_MEMORY_DESCRIPTOR for Kernel ease)
// Memory Map Entry Type (Matching UEFI Spec 2.9)
enum class MemoryType : uint32_t {
    Reserved = 0,
    LoaderCode = 1,
    LoaderData = 2,
    BootServicesCode = 3,
    BootServicesData = 4,
    RuntimeServicesCode = 5,
    RuntimeServicesData = 6,
    ConventionalMemory = 7,
    UnusableMemory = 8,
    ACPI_Reclaimable = 9,
    ACPI_NVS = 10,
    MMIO = 11,
    MMIO_PortSpace = 12,
    PalCode = 13,
    PersistentMemory = 14,
    MaxMemoryType = 15 // Etc
};

// Memory Descriptor (Must match EFI_MEMORY_DESCRIPTOR for raw casting)
struct MemoryDescriptor {
    uint32_t        type;           // MemoryType enum (32-bit)
    uint32_t        pad;            // Padding to align uint64 
    physical_addr_t physAddr;
    virtual_addr_t  virtAddr;
    uint64_t        numPages;
    uint64_t        flags;          // Attribute
} __attribute__((packed)); // Careful with packing. EFI is naturally aligned usually?
// Actually EFI_MEMORY_DESCRIPTOR is:
// UINT32 Type;
// UINT32 Padding; (If generic alignment rules apply, mostly yes for 64-bit)
// UINT64 Phys; ...
// But standard definition often doesn't show explicit padding, it relies on alignment.
// To be safe, let's look at EDK2.
// typedef struct { UINT32 Type; EFI_PHYSICAL_ADDRESS PhysicalStart; ... } 
// PhysicalStart is 64-bit.
// Offset of PhysicalStart is 8 usually due to alignment.
// So Type takes 4 bytes, then 4 bytes padding.
// My struct above has explicit pad.


struct FramebufferInfo {
    physical_addr_t baseAddress;
    uint32_t        width;
    uint32_t        height;
    uint32_t        pixelsPerScanLine;
    uint32_t        bytesPerPixel; // usually 4 (BGRA/RGBA)
    // Format could be an enum if needed
} __attribute__((packed));

struct BootInfo {
    uint64_t magic; // Integrity Check
    // Memory Map
    MemoryDescriptor* memoryMap;
    uint64_t          memoryMapSize;
    uint64_t          memoryMapDescriptorSize; // Size of individual descriptor in bytes

    // Graphics
    FramebufferInfo   framebuffer;

    // Hardware Discovery (ACPI)
    void*             rsdp; // Root System Description Pointer (Physical Address)

    // Kernel Location (for self-awareness)
    physical_addr_t   kernelBasePhys;
    uint64_t          kernelSize;

} __attribute__((packed));

#endif // MORPHIC_BOOT_INFO_H
