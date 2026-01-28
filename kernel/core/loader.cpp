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
    
    // Validate Bounds (Simplified)
    if (header.code_off > node->size || header.code_off + header.code_size > node->size) {
        result.error_code = -4; return result;
    }
    
    // Alloc Memory (+Padding for .bss section)
    // The binary's .bss section (uninitialized data) is not included in code_size
    // but needs virtual memory. The userspace runtime has a 2MB heap in .bss.
    // We reserve extra space (BSS_RESERVE) to cover typical .bss sizes.
    constexpr uint64_t BSS_RESERVE = 0x400000; // 4MB for .bss sections
    constexpr uint64_t ASSETS_GAP  = 0x400000; // 4MB gap before assets (after .bss)
    
    uint64_t alloc_size = header.code_size + BSS_RESERVE + ASSETS_GAP + header.assets_size; 
    uint64_t pages_needed = (alloc_size + 0xFFF) / 0x1000;
    
    void* phys_buffer = PMM::AllocContiguous(pages_needed);
    if (!phys_buffer) {
        result.error_code = -5; return result;
    }
    
    uint64_t phys_base = (uint64_t)phys_buffer;
    constexpr uint64_t KERNEL_RESERVED_SIZE = 0x10000000; // 256MB
    if (phys_base < KERNEL_RESERVED_SIZE) {
        result.error_code = -10; return result;
    }
    
    uint8_t* code_dest_phys = (uint8_t*)phys_base;
    kmemset(code_dest_phys, 0, pages_needed * 0x1000);
    
    // Load Code
    VFS::Read(node, header.code_off, header.code_size, code_dest_phys);

    // Calc Assets Dest - placed AFTER code + BSS_RESERVE + alignment gap
    // This ensures assets don't overlap with the .bss section
    uint8_t* assets_dest_phys = code_dest_phys + header.code_size + BSS_RESERVE + ASSETS_GAP;
    assets_dest_phys = (uint8_t*)(((uint64_t)assets_dest_phys + 0xFFF) & ~0xFFFULL); // Page align
    
    if (header.assets_size > 0) {
        VFS::Read(node, header.assets_off, header.assets_size, assets_dest_phys);
    }
    
    uint64_t user_base_addr = base_addr;
    if (user_base_addr == 0) user_base_addr = 0x600000000000ULL;

    // Map Pages
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t phys_addr = phys_base + (i * 0x1000);
        uint64_t virt_addr = user_base_addr + (i * 0x1000);
        MMU::MapPage(virt_addr, phys_addr, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_EXECUTABLE | PAGE_PRESENT);
    }
    
    uint8_t* code_dest = (uint8_t*)user_base_addr;
    uint64_t assets_offset = (uint64_t)assets_dest_phys - (uint64_t)code_dest_phys;
    uint8_t* assets_dest = (uint8_t*)(user_base_addr + assets_offset);
    
    // Alloc Stack
    uint64_t stack_base_virt = user_base_addr + 0x400000; // 4MB after code base
    uint64_t stack_size_pages = 4;
    
    for (uint64_t i = 0; i < stack_size_pages; i++) {
        void* phys_page = PMM::AllocPage();
        if (!phys_page) {
            result.error_code = -5; return result;
        }
        
        uint64_t stack_page_virt = stack_base_virt + i * 4096;
        if (!MMU::MapPage(stack_page_virt, (uint64_t)phys_page, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT)) {
             result.error_code = -6; return result;
        }
    }
    
    uint8_t* stack = (uint8_t*)(stack_base_virt + stack_size_pages * 4096);
    
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
