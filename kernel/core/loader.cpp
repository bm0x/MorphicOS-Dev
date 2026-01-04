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
    TRACE_CHECKPOINT("=== PACKAGE LOADER START ===");
    LoadedProcess result = {0, 0, -1};
    
    EarlyTerm::Print("[Loader] Loading: ");
    EarlyTerm::Print(path);
    EarlyTerm::Print("\n");
    
    TRACE_CHECKPOINT("Step 1: Opening VFS file");
    
    VFSNode* node = VFS::Open(path);
    if (!node) {
        KERNEL_PANIC_SAFE("Loader: File not found in VFS");
        result.error_code = -1; return result;
    }
    
    TRACE_HEX("Step 1: File opened, node", (uint64_t)node);
    TRACE_DEC("Step 1: File size", node->size);
    
    if (node->size < sizeof(MPKHeader)) {
        TRACE_DEC("ERROR: File size", node->size);
        TRACE_DEC("ERROR: Header size needed", sizeof(MPKHeader));
        KERNEL_PANIC_SAFE("Loader: File smaller than MPK header");
        result.error_code = -1; return result;
    }
    
    TRACE_CHECKPOINT("Step 2: Reading MPK header");

    constexpr uint32_t MPK_HEADER_SIZE = 64;
    uint8_t headerBuf[MPK_HEADER_SIZE];
    uint32_t toRead = (node->size < MPK_HEADER_SIZE) ? (uint32_t)node->size : MPK_HEADER_SIZE;
    uint32_t bytesRead = VFS::Read(node, 0, toRead, headerBuf);

    TRACE_DEC("Step 2: Bytes read", bytesRead);

    if (bytesRead < sizeof(MPKHeader)) {
        TRACE_DEC("ERROR: Expected at least bytes", sizeof(MPKHeader));
        KERNEL_PANIC_SAFE("Loader: Failed to read MPK header");
        result.error_code = -2; return result;
    }

    MPKHeader header;
    kmemcpy(&header, headerBuf, sizeof(MPKHeader));

    uint32_t total_size = 0;
    if (bytesRead >= 28) {
        total_size = *(uint32_t*)(headerBuf + 24);
    }
    
    TRACE_CHECKPOINT("Step 2b: Verifying magic bytes");
    TRACE_HEX("Step 2b: Magic[0-3]", 
              (header.magic[0] << 24) | (header.magic[1] << 16) | 
              (header.magic[2] << 8) | header.magic[3]);
    
    if (!VerifyMagic(&header)) {
        UART::Write("ERROR: Expected 'MPK1', got '");
        UART::PutChar(header.magic[0]);
        UART::PutChar(header.magic[1]);
        UART::PutChar(header.magic[2]);
        UART::PutChar(header.magic[3]);
        UART::Write("'\n");
        KERNEL_PANIC_SAFE("Loader: Invalid MPK magic bytes");
        result.error_code = -3; return result;
    }
    
    TRACE_CHECKPOINT("Step 3: Header validated OK");
    TRACE_HEX("  manifest_off", header.manifest_off);
    TRACE_HEX("  code_off", header.code_off);
    TRACE_DEC("  code_size", header.code_size);
    TRACE_HEX("  assets_off", header.assets_off);
    TRACE_DEC("  assets_size", header.assets_size);
    if (total_size) {
        TRACE_DEC("  total_size", total_size);
    }
    
    EarlyTerm::Print("[Loader] Code: ");
    EarlyTerm::PrintDec(header.code_size);
    EarlyTerm::Print(" bytes, Assets: ");
    EarlyTerm::PrintDec(header.assets_size);
    EarlyTerm::Print(" bytes\n");

    if (total_size && total_size != node->size) {
        TRACE_DEC("ERROR: total_size", total_size);
        TRACE_DEC("ERROR: file size", node->size);
        KERNEL_PANIC_SAFE("Loader: MPK total_size mismatch");
        result.error_code = -4; return result;
    }

    if (header.manifest_off != 0) {
        if (header.manifest_off < MPK_HEADER_SIZE) {
            TRACE_HEX("ERROR: manifest_off < header", header.manifest_off);
            KERNEL_PANIC_SAFE("Loader: manifest_off invalid");
            result.error_code = -4; return result;
        }
        if (header.manifest_off >= header.code_off) {
            TRACE_HEX("ERROR: manifest_off", header.manifest_off);
            TRACE_HEX("ERROR: code_off", header.code_off);
            KERNEL_PANIC_SAFE("Loader: manifest overlaps code");
            result.error_code = -4; return result;
        }
    }
    
    if (header.code_off > node->size) {
        TRACE_HEX("ERROR: code_off out of bounds", header.code_off);
        KERNEL_PANIC_SAFE("Loader: code_off exceeds file size");
        result.error_code = -4; return result;
    }
    
    if (header.code_off + header.code_size > node->size) {
        TRACE_HEX("ERROR: code segment end", header.code_off + header.code_size);
        TRACE_HEX("ERROR: file size", node->size);
        KERNEL_PANIC_SAFE("Loader: Code segment exceeds file bounds");
        result.error_code = -4; return result;
    }
    
    if (header.assets_size > 0) {
        if (header.assets_off > node->size) {
            TRACE_HEX("ERROR: assets_off out of bounds", header.assets_off);
            KERNEL_PANIC_SAFE("Loader: assets_off exceeds file size");
            result.error_code = -4; return result;
        }
        
        if (header.assets_off + header.assets_size > node->size) {
            TRACE_HEX("ERROR: assets segment end", header.assets_off + header.assets_size);
            KERNEL_PANIC_SAFE("Loader: Assets segment exceeds file bounds");
            result.error_code = -4; return result;
        }
    }
    
    TRACE_CHECKPOINT("Step 3b: Bounds validation PASSED");
    
    TRACE_CHECKPOINT("Step 4: Allocating memory");
    
    uint64_t alloc_size = header.code_size + header.assets_size + 8192 + 0x10000;
    TRACE_DEC("Step 4: Total allocation size", alloc_size);
    
    uint64_t pages_needed = (alloc_size + 0xFFF) / 0x1000;
    TRACE_DEC("Step 4: Allocating pages", pages_needed);
    
    void* phys_buffer = PMM::AllocContiguous(pages_needed);
    if (!phys_buffer) {
        KERNEL_PANIC_SAFE("Loader: OOM - Failed to allocate contiguous pages");
        result.error_code = -5; return result;
    }
    
    uint64_t phys_base = (uint64_t)phys_buffer;
    TRACE_HEX("Step 4: Physical base", phys_base);
    
    constexpr uint64_t KERNEL_RESERVED_SIZE = 0x10000000; // 256MB
    if (phys_base < KERNEL_RESERVED_SIZE) {
        TRACE_HEX("FATAL: Allocated in kernel zone!", phys_base);
        KERNEL_PANIC_SAFE("Loader: PMM returned kernel-reserved memory!");
        result.error_code = -10; return result;
    }
    
    TRACE_CHECKPOINT("Step 4b: Safety check passed");
    
    uint8_t* code_dest_phys = (uint8_t*)phys_base;
    
    TRACE_CHECKPOINT("Step 4c: Clearing allocated memory");
    kmemset(code_dest_phys, 0, pages_needed * 0x1000);
    
    TRACE_HEX("Step 4d: Buffer cleared at phys", (uint64_t)code_dest_phys);
    
    TRACE_CHECKPOINT("Step 5: Loading code payload");
    
    bytesRead = VFS::Read(node, header.code_off, header.code_size, code_dest_phys);
    
    TRACE_DEC("Step 5: Code bytes read", bytesRead);
    
    if (bytesRead != header.code_size) {
        TRACE_DEC("ERROR: Expected bytes", header.code_size);
        KERNEL_PANIC_SAFE("Loader: Failed to read code payload");
        result.error_code = -6; return result;
    }
    
    UART::Write("Step 5: First 16 bytes of code: ");
    for (int i = 0; i < 16 && i < (int)header.code_size; i++) {
        UART::WriteHex(code_dest_phys[i]);
        UART::Write(" ");
    }
    UART::Write("\n");

    bool all_zero = true;
    uint32_t check_len = (header.code_size < 64) ? header.code_size : 64;
    for (uint32_t i = 0; i < check_len; i++) {
        if (code_dest_phys[i] != 0x00) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        KERNEL_PANIC_SAFE("Invalid MPK: Code segment begins with all zeros");
        result.error_code = -8; return result;
    }
    
    uint8_t* assets_dest_phys = code_dest_phys + header.code_size + 4096;
    assets_dest_phys = (uint8_t*)(((uint64_t)assets_dest_phys + 15) & ~0xFULL);
    
    TRACE_HEX("Step 6: Assets physical dest", (uint64_t)assets_dest_phys);
    
    if (header.assets_size > 0) {
        TRACE_CHECKPOINT("Step 6: Loading assets");
        bytesRead = VFS::Read(node, header.assets_off, header.assets_size, assets_dest_phys);
        TRACE_DEC("Step 6: Assets bytes read", bytesRead);
    }
    
    uint64_t user_base_addr = base_addr;
    if (user_base_addr == 0) user_base_addr = 0x600000000000ULL;

    TRACE_CHECKPOINT("Step 6b: Mapping to user VA");
    
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t phys_addr = phys_base + (i * 0x1000);
        uint64_t virt_addr = user_base_addr + (i * 0x1000);
        MMU::MapPage(virt_addr, phys_addr, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_EXECUTABLE | PAGE_PRESENT);
    }
    
    uint8_t* code_dest = (uint8_t*)user_base_addr;
    uint64_t assets_offset = (uint64_t)assets_dest_phys - (uint64_t)code_dest_phys;
    uint8_t* assets_dest = (uint8_t*)(user_base_addr + assets_offset);
    
    TRACE_HEX("Step 6b: User code VA", (uint64_t)code_dest);
    TRACE_HEX("Step 6b: User assets VA", (uint64_t)assets_dest);
    
    TRACE_CHECKPOINT("Step 7: Allocating user stack");
    
    uint64_t stack_base_virt = user_base_addr + 0x400000; // 4MB after code base
    uint64_t stack_size_pages = 4;
    
    for (uint64_t i = 0; i < stack_size_pages; i++) {
        void* phys_page = PMM::AllocPage();
        if (!phys_page) {
            KERNEL_PANIC_SAFE("Loader: OOM allocating user stack");
            result.error_code = -5; return result;
        }
        
        uint64_t stack_page_virt = stack_base_virt + i * 4096;
        bool mapped = MMU::MapPage(stack_page_virt, (uint64_t)phys_page, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        if (!mapped) {
            UART::Write("[Loader] ERROR: Failed to map stack page at: ");
            UART::WriteHex(stack_page_virt);
            UART::Write("\n");
            result.error_code = -6; return result;
        }
        
        uint64_t check_phys = MMU::GetPhysical(stack_page_virt);
        if (check_phys != (uint64_t)phys_page) {
            UART::Write("[Loader] CRITICAL: MapPage succeeded but GetPhysical failed!\n");
            result.error_code = -7; return result;
        }
    }
    
    uint8_t* stack = (uint8_t*)(stack_base_virt + stack_size_pages * 4096);
    
    TRACE_HEX("Step 7: Stack base", stack_base_virt);
    TRACE_HEX("Step 7: Stack top", (uint64_t)stack);
    
    TRACE_CHECKPOINT("=== PRE-EXECUTION SUMMARY ===");
    TRACE_HEX("  Entry Point: ", (uint64_t)user_base_addr);
    TRACE_HEX("  Stack Pointer: ", (uint64_t)stack);
    TRACE_HEX("  Assets Pointer (arg1): ", (uint64_t)assets_dest);
    
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    UART::Write("[Loader] Reloading CR3: "); UART::WriteHex(cr3); UART::Write("\n");
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    
    result.entry_point = (uint64_t)code_dest;
    result.stack_top = (uint64_t)stack;
    result.error_code = 0;
    
    return result;
}
