#include "loader.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../arch/common/mmu.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"
#include "../hal/arch/x86_64/syscall.h"
#include "../hal/serial/uart.h"
#include "debug.h"

bool PackageLoader::VerifyMagic(const MPKHeader* header) {
    if (header->magic[0] == 'M' && header->magic[1] == 'P' &&
        header->magic[2] == 'K' && header->magic[3] == '1') {
        return true;
    }
    return false;
}

LoadedProcess PackageLoader::Load(const char* path, uint64_t base_addr) {
    // Optimized Loader - Minimal Logging
    LoadedProcess result = {0, 0, -1};
    VFSNode* node = VFS::Open(path);
    if (!node) {
        result.error_code = -1; return result;
    }
    
    if (node->size < sizeof(MPKHeader)) {
        result.error_code = -1; return result;
    }
    
    constexpr uint32_t MPK_HEADER_SIZE = 64;
    uint8_t headerBuf[MPK_HEADER_SIZE];
    uint32_t toRead = (node->size < MPK_HEADER_SIZE) ? (uint32_t)node->size : MPK_HEADER_SIZE;
    VFS::Read(node, 0, toRead, headerBuf);

    MPKHeader header;
    kmemcpy(&header, headerBuf, sizeof(MPKHeader));
    
    if (!VerifyMagic(&header)) {
        result.error_code = -3; return result;
    }
    
    // Validate Bounds (Overflow-safe)
    if (header.code_size == 0 ||
        header.code_off > node->size ||
        header.code_size > (node->size - header.code_off)) {
        result.error_code = -4; return result;
    }
    if (header.assets_size > 0 &&
        (header.assets_off > node->size || header.assets_size > (node->size - header.assets_off))) {
        result.error_code = -4; return result;
    }

    uint64_t user_base_addr = (base_addr != 0) ? base_addr : 0x600000000000ULL;

    // 1. Allocate Code + .bss pages (Canonical region: 0x6000_0000_0000)
    // Code pages get RX/RW as required by flat binary; pure .bss pages get RW+NX (non-executable).
    constexpr uint64_t BSS_RESERVE = 0x400000ULL; // 4MB for .bss (covers 2MB SDK pool + globals)
    uint64_t code_pages = (header.code_size + 0xFFFULL) / 0x1000ULL;
    uint64_t bss_pages  = BSS_RESERVE / 0x1000ULL;
    uint64_t total_image_pages = code_pages + bss_pages;

    void* phys_buffer = PMM::AllocContiguous(total_image_pages);
    if (!phys_buffer) {
        result.error_code = -5; return result;
    }

    uint64_t phys_base = (uint64_t)phys_buffer;
    constexpr uint64_t KERNEL_RESERVED_SIZE = 0x10000000ULL; // 256MB
    if (phys_base < KERNEL_RESERVED_SIZE) {
        PMM::FreePages(phys_buffer, total_image_pages);
        result.error_code = -10; return result;
    }

    uint8_t* code_dest_phys = (uint8_t*)phys_base;
    kmemset(code_dest_phys, 0, total_image_pages * 0x1000ULL);
    VFS::Read(node, header.code_off, header.code_size, code_dest_phys);

    for (uint64_t i = 0; i < total_image_pages; i++) {
        uint64_t phys_addr = phys_base + (i * 0x1000ULL);
        uint64_t virt_addr = user_base_addr + (i * 0x1000ULL);
        // Pure .bss pages strictly enforce NX (non-executable)
        uint32_t page_flags = (i < code_pages)
            ? (PAGE_USER | PAGE_WRITABLE | PAGE_EXECUTABLE | PAGE_PRESENT)
            : (PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        if (!MMU::MapPage(virt_addr, phys_addr, page_flags)) {
            result.error_code = -6; return result;
        }
    }

    // 2. Map MPK Assets in dedicated read-only + NX window (0x6000_2000_0000 per Architecture Spec §2)
    uint64_t assets_virt_base = 0x600020000000ULL;
    uint8_t* assets_dest = nullptr;
    if (header.assets_size > 0) {
        uint64_t asset_pages = (header.assets_size + 0xFFFULL) / 0x1000ULL;
        void* assets_phys = PMM::AllocContiguous(asset_pages);
        if (!assets_phys) {
            result.error_code = -5; return result;
        }
        kmemset(assets_phys, 0, asset_pages * 0x1000ULL);
        VFS::Read(node, header.assets_off, header.assets_size, (uint8_t*)assets_phys);

        for (uint64_t i = 0; i < asset_pages; i++) {
            // Read-Only + NX (PAGE_USER | PAGE_PRESENT)
            if (!MMU::MapPage(assets_virt_base + (i * 0x1000ULL),
                              (uint64_t)assets_phys + (i * 0x1000ULL),
                              PAGE_USER | PAGE_PRESENT)) {
                result.error_code = -6; return result;
            }
        }
        assets_dest = (uint8_t*)assets_virt_base;
    }

    uint8_t* code_dest = (uint8_t*)user_base_addr;

    // 3. Alloc Stack in isolated userspace high virtual region (0x6FFFFFFF0000)
    // Leave guard page at stack_base_virt - 4096 unmapped (PROT_NONE) to catch stack overflows.
    constexpr uint64_t USER_STACK_TOP = 0x6FFFFFFF0000ULL;
    constexpr uint64_t USER_STACK_PAGES = 16; // 64KB stack
    uint64_t stack_base_virt = USER_STACK_TOP - (USER_STACK_PAGES * 4096ULL);
    MMU::UnmapPage(stack_base_virt - 4096ULL); // Explicit stack guard page

    for (uint64_t i = 0; i < USER_STACK_PAGES; i++) {
        void* phys_page = PMM::AllocPage();
        if (!phys_page) {
            result.error_code = -5; return result;
        }
        kmemset(phys_page, 0, 4096);

        uint64_t stack_page_virt = stack_base_virt + (i * 4096ULL);
        if (!MMU::MapPage(stack_page_virt, (uint64_t)phys_page,
                          PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT)) {
            PMM::FreePage(phys_page);
            result.error_code = -6; return result;
        }
    }
    
    uint8_t* stack = (uint8_t*)USER_STACK_TOP;
    
    // Reload CR3 to flush TLB (Essential)
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    
    result.entry_point = (uint64_t)code_dest;
    result.stack_top = (uint64_t)stack;
    // Pass assets pointer as first argument to user entry (RDI)
    result.arg1 = (uint64_t)assets_dest;
    result.error_code = 0;
    
    return result;
}
