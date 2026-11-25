#pragma once
#include <stdint.h>

// ============================================================================
// MorphicVK 1.3 — Real-Time Vulkan Translation Layer UAPI (Kernel <-> Userspace)
// Bridges Vulkan-style command buffers onto PCI GPUs (NVIDIA/AMD/Intel/VirtIO)
// using SIMD + Write-Combining GOP Framebuffers without proprietary drivers.
// ============================================================================

#define MORPHIC_VK_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

#define MORPHIC_VK_API_VERSION_1_3 MORPHIC_VK_MAKE_VERSION(1, 3, 0)

// Device Capability Flags
#define MVK_DEVICE_FLAG_PCI_DETECTED       (1u << 0)
#define MVK_DEVICE_FLAG_REALTIME_TRANSLATE (1u << 1)
#define MVK_DEVICE_FLAG_SIMD_SSE2          (1u << 2)
#define MVK_DEVICE_FLAG_WRITE_COMBINING    (1u << 3)
#define MVK_DEVICE_FLAG_ATOMIC_PRESENT     (1u << 4)
#define MVK_DEVICE_FLAG_DISCRETE_GPU       (1u << 5)

// Vulkan Queue Family Equivalents
#define MVK_QUEUE_GRAPHICS_BIT (1u << 0)
#define MVK_QUEUE_COMPUTE_BIT  (1u << 1)
#define MVK_QUEUE_TRANSFER_BIT (1u << 2)

// Command Stream Opcodes
enum MorphicVkOpcode : uint16_t {
    MVK_CMD_NOP            = 0,
    MVK_CMD_CLEAR_COLOR    = 1,
    MVK_CMD_FILL_RECT      = 2,
    MVK_CMD_BLEND_RECT     = 3,
    MVK_CMD_DRAW_LINE      = 4,
    MVK_CMD_DRAW_TRIANGLE  = 5,
    MVK_CMD_COMPUTE_XOR32  = 6,
    MVK_CMD_PRESENT_DAMAGE = 7,
};

struct MorphicVkPhysicalDeviceInfo {
    uint32_t api_version;         // MORPHIC_VK_API_VERSION_1_3
    uint32_t driver_version;
    uint16_t vendor_id;           // 0x10DE=NVIDIA, 0x1002=AMD, 0x8086=Intel, etc.
    uint16_t device_id;
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  queue_flags;         // MVK_QUEUE_GRAPHICS_BIT | COMPUTE | TRANSFER
    uint32_t device_flags;        // MVK_DEVICE_FLAG_*
    uint64_t vram_bar_phys;       // Hardware PCI BAR0/GOP base
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t reserved0;
    uint64_t translated_batches;  // Total command buffers translated in real time
    uint64_t translated_cmds;     // Total primitives/compute ops translated
    char     vendor_name[24];     // e.g. "NVIDIA", "AMD", "INTEL"
    char     device_name[64];     // e.g. "NVIDIA GeForce/RTX GPU (0x2684)"
    char     layer_name[48];      // "MorphicVK 1.3 Real-Time Translation Layer"
};

struct MorphicVkCmdPacket {
    uint16_t opcode;      // MorphicVkOpcode
    uint16_t flags;       // e.g. bit 0 = wait_vsync on PRESENT_DAMAGE
    uint32_t color_argb;  // 0xAARRGGBB or compute immediate
    int16_t  x0;
    int16_t  y0;
    int16_t  x1;          // width for RECT, x1 for LINE/TRIANGLE
    int16_t  y1;          // height for RECT, y1 for LINE/TRIANGLE
    int16_t  x2;          // x2 for TRIANGLE
    int16_t  y2;          // y2 for TRIANGLE
    uint32_t reserved;    // Total packet = 24 bytes (8-byte aligned)
};

struct MorphicVkSubmitInfo {
    uint64_t target_surface_virt; // 0 = DRM primary compositor buffer, else user ARGB32 surface
    uint32_t target_width;
    uint32_t target_height;
    uint32_t target_stride_px;    // 0 = tightly packed (target_width)
    uint32_t cmd_count;
    uint64_t cmd_packets_ptr;     // Pointer to MorphicVkCmdPacket[cmd_count]
    uint32_t flags;               // bit 0 = auto-mark DRM dirty
    uint32_t reserved;
};
