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

// ============================================================================
// DEBUG FLAGS - Uncomment to enable specific debugging modes
// ============================================================================

// When enabled, runs the user code in Ring 0 (kernel mode) to test if
// the code itself is valid before attempting the Ring 3 transition.
// If this works but Ring 3 doesn't, the issue is memory permissions.
// #define DEBUG_RING0_ONLY

// ============================================================================
// Package Loader Implementation
// ============================================================================

bool PackageLoader::VerifyMagic(const MPKHeader* header) {
    if (header->magic[0] == 'M' && header->magic[1] == 'P' &&
        header->magic[2] == 'K' && header->magic[3] == '1') {
        return true;
    }
    return false;
}

int PackageLoader::Load(const char* path) {
    TRACE_CHECKPOINT("=== PACKAGE LOADER START ===");
    
    EarlyTerm::Print("[Loader] Loading: ");
    EarlyTerm::Print(path);
    EarlyTerm::Print("\n");
    
    // ========================================================================
    // STEP 1: Open file via VFS
    // ========================================================================
    TRACE_CHECKPOINT("Step 1: Opening VFS file");
    
    VFSNode* node = VFS::Open(path);
    if (!node) {
        KERNEL_PANIC_SAFE("Loader: File not found in VFS");
        return -1;
    }
    
    TRACE_HEX("Step 1: File opened, node", (uint64_t)node);
    TRACE_DEC("Step 1: File size", node->size);
    
    // ========================================================================
    // GUARD: File must be larger than MPKHeader
    // ========================================================================
    if (node->size < sizeof(MPKHeader)) {
        TRACE_DEC("ERROR: File size", node->size);
        TRACE_DEC("ERROR: Header size needed", sizeof(MPKHeader));
        KERNEL_PANIC_SAFE("Loader: File smaller than MPK header");
        return -1;
    }
    
    // ========================================================================
    // STEP 2: Read and validate MPK header
    // ========================================================================
    TRACE_CHECKPOINT("Step 2: Reading MPK header");

    constexpr uint32_t MPK_HEADER_SIZE = 64; // MPK1 convention: 24-byte header + reserved padding
    uint8_t headerBuf[MPK_HEADER_SIZE];
    uint32_t toRead = (node->size < MPK_HEADER_SIZE) ? (uint32_t)node->size : MPK_HEADER_SIZE;
    uint32_t bytesRead = VFS::Read(node, 0, toRead, headerBuf);

    TRACE_DEC("Step 2: Bytes read", bytesRead);

    if (bytesRead < sizeof(MPKHeader)) {
        TRACE_DEC("ERROR: Expected at least bytes", sizeof(MPKHeader));
        KERNEL_PANIC_SAFE("Loader: Failed to read MPK header");
        return -2;
    }

    MPKHeader header;
    kmemcpy(&header, headerBuf, sizeof(MPKHeader));

    // Optional forward-compat fields stored in header padding.
    // Convention: uint32_t total_size at byte offset 24.
    uint32_t total_size = 0;
    if (bytesRead >= 28) {
        total_size = *(uint32_t*)(headerBuf + 24);
    }
    
    // ========================================================================
    // GUARD: Verify magic bytes
    // ========================================================================
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
        return -3;
    }
    
    // ========================================================================
    // STEP 3: Log header values
    // ========================================================================
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

    // ========================================================================
    // GUARD: Validate total size if present
    // ========================================================================
    if (total_size && total_size != node->size) {
        TRACE_DEC("ERROR: total_size", total_size);
        TRACE_DEC("ERROR: file size", node->size);
        KERNEL_PANIC_SAFE("Loader: MPK total_size mismatch");
        return -4;
    }

    // ========================================================================
    // GUARD: Validate manifest offset if present (manifest is optional)
    // ========================================================================
    if (header.manifest_off != 0) {
        if (header.manifest_off < MPK_HEADER_SIZE) {
            TRACE_HEX("ERROR: manifest_off < header", header.manifest_off);
            KERNEL_PANIC_SAFE("Loader: manifest_off invalid");
            return -4;
        }
        if (header.manifest_off >= header.code_off) {
            TRACE_HEX("ERROR: manifest_off", header.manifest_off);
            TRACE_HEX("ERROR: code_off", header.code_off);
            KERNEL_PANIC_SAFE("Loader: manifest overlaps code");
            return -4;
        }
    }
    
    // ========================================================================
    // GUARD: Validate code offset and size within file bounds
    // ========================================================================
    if (header.code_off > node->size) {
        TRACE_HEX("ERROR: code_off out of bounds", header.code_off);
        KERNEL_PANIC_SAFE("Loader: code_off exceeds file size");
        return -4;
    }
    
    if (header.code_off + header.code_size > node->size) {
        TRACE_HEX("ERROR: code segment end", header.code_off + header.code_size);
        TRACE_HEX("ERROR: file size", node->size);
        KERNEL_PANIC_SAFE("Loader: Code segment exceeds file bounds");
        return -4;
    }
    
    // ========================================================================
    // GUARD: Validate assets offset and size within file bounds
    // ========================================================================
    if (header.assets_size > 0) {
        if (header.assets_off > node->size) {
            TRACE_HEX("ERROR: assets_off out of bounds", header.assets_off);
            KERNEL_PANIC_SAFE("Loader: assets_off exceeds file size");
            return -4;
        }
        
        if (header.assets_off + header.assets_size > node->size) {
            TRACE_HEX("ERROR: assets segment end", header.assets_off + header.assets_size);
            KERNEL_PANIC_SAFE("Loader: Assets segment exceeds file bounds");
            return -4;
        }
    }
    
    TRACE_CHECKPOINT("Step 3b: Bounds validation PASSED");
    
    // ========================================================================
    // STEP 4: Allocate memory for code and stack
    // ========================================================================
    TRACE_CHECKPOINT("Step 4: Allocating memory");
    
    // Add 64KB padding for safety (BSS/Heap growth)
    uint64_t alloc_size = header.code_size + header.assets_size + 8192 + 0x10000;
    TRACE_DEC("Step 4: Total allocation size", alloc_size);
    
    // ========================================================================
    // SIMPLE APPROACH: Allocate contiguous physical memory from PMM
    // PMM::AllocContiguous guarantees contiguous pages starting at >= 256MB
    // ========================================================================
    uint64_t pages_needed = (alloc_size + 0xFFF) / 0x1000;
    TRACE_DEC("Step 4: Allocating pages", pages_needed);
    
    void* phys_buffer = PMM::AllocContiguous(pages_needed);
    if (!phys_buffer) {
        KERNEL_PANIC_SAFE("Loader: OOM - Failed to allocate contiguous pages");
        return -5;
    }
    
    uint64_t phys_base = (uint64_t)phys_buffer;
    TRACE_HEX("Step 4: Physical base", phys_base);
    
    // ========================================================================
    // SAFETY CHECK: Ensure we're not in kernel reserved zone (0 - 256MB)
    // ========================================================================
    constexpr uint64_t KERNEL_RESERVED_SIZE = 0x10000000; // 256MB
    if (phys_base < KERNEL_RESERVED_SIZE) {
        TRACE_HEX("FATAL: Allocated in kernel zone!", phys_base);
        KERNEL_PANIC_SAFE("Loader: PMM returned kernel-reserved memory!");
        return -10;
    }
    
    TRACE_CHECKPOINT("Step 4b: Safety check passed");
    
    // Use physical base directly via identity map for writing
    uint8_t* code_dest_phys = (uint8_t*)phys_base;
    
    // Clear all pages via identity map
    TRACE_CHECKPOINT("Step 4c: Clearing allocated memory");
    kmemset(code_dest_phys, 0, pages_needed * 0x1000);
    
    TRACE_HEX("Step 4d: Buffer cleared at phys", (uint64_t)code_dest_phys);
    
    // ========================================================================
    // STEP 5: Load code payload (write to PHYSICAL address via identity map)
    // ========================================================================
    TRACE_CHECKPOINT("Step 5: Loading code payload");
    
    bytesRead = VFS::Read(node, header.code_off, header.code_size, code_dest_phys);
    
    TRACE_DEC("Step 5: Code bytes read", bytesRead);
    
    if (bytesRead != header.code_size) {
        TRACE_DEC("ERROR: Expected bytes", header.code_size);
        KERNEL_PANIC_SAFE("Loader: Failed to read code payload");
        return -6;
    }
    
    // Log first bytes of code for debugging
    UART::Write("Step 5: First 16 bytes of code: ");
    for (int i = 0; i < 16 && i < (int)header.code_size; i++) {
        UART::WriteHex(code_dest_phys[i]);
        UART::Write(" ");
    }
    UART::Write("\n");

    // VALIDATION: reject obviously-empty code segment (all zeros at start)
    // (Do not require the first dword to be nonzero; that's too strict.)
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
        return -8;
    }
    
    // ========================================================================
    // STEP 6: Load assets (write to PHYSICAL address)
    // ========================================================================
    uint8_t* assets_dest_phys = code_dest_phys + header.code_size + 4096;
    assets_dest_phys = (uint8_t*)(((uint64_t)assets_dest_phys + 15) & ~0xFULL);
    
    TRACE_HEX("Step 6: Assets physical dest", (uint64_t)assets_dest_phys);
    
    if (header.assets_size > 0) {
        TRACE_CHECKPOINT("Step 6: Loading assets");
        bytesRead = VFS::Read(node, header.assets_off, header.assets_size, assets_dest_phys);
        TRACE_DEC("Step 6: Assets bytes read", bytesRead);
    }
    
    // ========================================================================
    // STEP 6b: Map physical pages to User Virtual Address (512GB)
    // ========================================================================
    // Userspace virtual address base
    // NOTE: 0x8000000000 collides with UEFI identity-mapping huge pages in PML4[1]
    // Using 0x600000000000 (PML4 index 192) which is typically unused by UEFI
    uint64_t user_base_addr = 0x600000000000ULL;
    TRACE_CHECKPOINT("Step 6b: Mapping to user VA");
    
    // Map each page from phys_base to user_base_addr
    // Since PMM may not give contiguous pages, we re-read from phys_base
    // and map in 4KB increments (assuming they ARE contiguous for now)
    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t phys_addr = phys_base + (i * 0x1000);
        uint64_t virt_addr = user_base_addr + (i * 0x1000);
        MMU::MapPage(virt_addr, phys_addr, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_EXECUTABLE | PAGE_PRESENT);
    }
    
    // User-visible addresses
    uint8_t* code_dest = (uint8_t*)user_base_addr;
    uint64_t assets_offset = (uint64_t)assets_dest_phys - (uint64_t)code_dest_phys;
    uint8_t* assets_dest = (uint8_t*)(user_base_addr + assets_offset);
    
    TRACE_HEX("Step 6b: User code VA", (uint64_t)code_dest);
    TRACE_HEX("Step 6b: User assets VA", (uint64_t)assets_dest);
    
    // ========================================================================
    // STEP 7: Allocating user stack
    // ========================================================================
    TRACE_CHECKPOINT("Step 7: Allocating user stack");
    
    // User stack virtual address
    // NOTE: Using address close to code to be in same Page Directory entry
    // Code is at 0x600000000000 (PD[0]), let's put stack at 0x600000400000 (PD[2])
    // This tests if the issue is with creating new PD entries
    uint64_t stack_base_virt = 0x600000400000ULL; // 4MB after code base
    uint64_t stack_size_pages = 4;
    
    for (uint64_t i = 0; i < stack_size_pages; i++) {
        void* phys_page = PMM::AllocPage();
        if (!phys_page) {
            KERNEL_PANIC_SAFE("Loader: OOM allocating user stack");
            return -5;
        }
        // NOTE: Do NOT clear phys_page directly - it may not be identity-mapped!
        // PMM::AllocPage() returns addresses >= 256MB which UEFI may not have mapped.
        // The stack will be cleared after mapping via the virtual address.
        
        uint64_t stack_page_virt = stack_base_virt + i * 4096;
        bool mapped = MMU::MapPage(stack_page_virt, (uint64_t)phys_page, 
                     PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        if (!mapped) {
            UART::Write("[Loader] ERROR: Failed to map stack page at: ");
            UART::WriteHex(stack_page_virt);
            UART::Write("\n");
            return -6;
        }
        
        // VERIFY MAPPING
        uint64_t check_phys = MMU::GetPhysical(stack_page_virt);
        if (check_phys != (uint64_t)phys_page) {
            UART::Write("[Loader] CRITICAL: MapPage succeeded but GetPhysical failed!\n");
            UART::Write("  Virt: "); UART::WriteHex(stack_page_virt); UART::Write("\n");
            UART::Write("  Expected Phys: "); UART::WriteHex((uint64_t)phys_page); UART::Write("\n");
            UART::Write("  Actual Phys:   "); UART::WriteHex(check_phys); UART::Write("\n");
            return -7;
        }
    }
    
    // Stack grows DOWN, so top is base + size
    uint8_t* stack = (uint8_t*)(stack_base_virt + stack_size_pages * 4096);
    
    // NOTE: We cannot clear the stack memory from kernel mode because:
    // 1. The virtual addresses (0x600000800000+) are only mapped for userspace
    // 2. The physical pages are not identity-mapped
    // The userspace startup code should handle stack initialization.
    
    TRACE_HEX("Step 7: Stack base", stack_base_virt);
    TRACE_HEX("Step 7: Stack top", (uint64_t)stack);
    
    // ========================================================================
    // STEP 8: Prepare for userspace transition
    // ========================================================================
    TRACE_CHECKPOINT("=== PRE-EXECUTION SUMMARY ===");
    TRACE_HEX("  Entry Point: ", (uint64_t)user_base_addr);
    TRACE_HEX("  Stack Pointer: ", (uint64_t)stack);
    TRACE_HEX("  Assets Pointer (arg1): ", (uint64_t)assets_dest);
    
    // CRITICAL: Reload CR3 to flush all TLB and Paging Structure Caches
    // We modified page tables (potentially adding new directories) logic requires effective flush
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    UART::Write("[Loader] Reloading CR3: "); UART::WriteHex(cr3); UART::Write("\n");
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    
    TRACE_CHECKPOINT("Step 8: Calling JumpToUser (IRETQ to Ring 3)");
    UART::Write(">>> If system reboots after this, check Page Fault in serial <<<\n\n");
    
    Syscall::JumpToUser((void*)user_base_addr, (void*)stack, (void*)assets_dest);

#ifdef DEBUG_RING0_ONLY
    // ========================================================================
    // DEBUG MODE: Execute in Ring 0 to test code validity
    // ========================================================================
    TRACE_CHECKPOINT("!!! DEBUG_RING0_ONLY: Calling code in Ring 0 !!!");
    UART::Write("This bypasses Ring 3 transition to test if code is valid.\n");
    UART::Write("If this works but normal mode doesn't, the issue is PAGE_USER.\n\n");
    
    typedef void (*EntryFunc)(void*);
    EntryFunc entry = (EntryFunc)code_dest;
    entry((void*)assets_dest);
    
    KERNEL_PANIC_SAFE("DEBUG: User code returned (should not happen)");
#else
    // ========================================================================
    // NORMAL MODE: Jump to Ring 3 via IRETQ
    // ========================================================================
    TRACE_CHECKPOINT("Step 8: Calling JumpToUser (IRETQ to Ring 3)");
    UART::Write(">>> If system reboots after this, check Page Fault in serial <<<\n\n");
    
    Syscall::JumpToUser((void*)code_dest, (void*)stack, (void*)assets_dest);
#endif
    
    // Should never reach here
    KERNEL_PANIC_SAFE("Loader: JumpToUser returned unexpectedly");
    return 0;
}
