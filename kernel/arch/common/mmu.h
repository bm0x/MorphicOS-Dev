#pragma once

// Memory Management Unit Abstraction
// Architecture-independent virtual memory interface

#include <stdint.h>

// Page flags (architecture-agnostic)
#define PAGE_PRESENT     (1 << 0)   // Page is present in memory
#define PAGE_WRITABLE    (1 << 1)   // Page is writable
#define PAGE_USER        (1 << 2)   // Page accessible from user mode
#define PAGE_EXECUTABLE  (1 << 3)   // Page contains executable code
#define PAGE_NOCACHE     (1 << 4)   // Disable caching
#define PAGE_WRITETHROUGH (1 << 5)  // Write-through caching
#define PAGE_GLOBAL      (1 << 6)   // Global page (not flushed on context switch)

// Page sizes
#define PAGE_SIZE_4K     0x1000
#define PAGE_SIZE_2M     0x200000
#define PAGE_SIZE_1G     0x40000000

// Address masks
#define PAGE_MASK_4K     0xFFFFFFFFFFFFF000ULL
#define PAGE_MASK_2M     0xFFFFFFFFFFE00000ULL

namespace MMU {
    // Initialize the MMU with identity mapping for kernel
    void Init();
    
    // Map a virtual address to a physical address
    // Returns true on success
    bool MapPage(uint64_t virt, uint64_t phys, uint32_t flags);
    
    // Map a range of pages
    bool MapRange(uint64_t virt_start, uint64_t phys_start, 
                  uint64_t size, uint32_t flags);
    
    // Unmap a virtual address
    void UnmapPage(uint64_t virt);
    
    // Flush TLB for a specific address
    void FlushTLB(uint64_t virt);
    
    // Flush entire TLB
    void FlushTLBAll();
    
    // Get physical address for a virtual address
    // Returns 0 if not mapped
    uint64_t GetPhysical(uint64_t virt);
    
    // Switch to a different page table
    // x86: writes to CR3
    // ARM: writes to TTBR0/TTBR1
    void SwitchPageTable(uint64_t table_phys);
    
    // Get current page table address
    uint64_t GetCurrentPageTable();
    
    // Create a new page table (allocates from heap)
    uint64_t CreatePageTable();
    
    // Clone current page table (for fork)
    uint64_t ClonePageTable();
    
    // Destroy a page table (frees memory)
    void DestroyPageTable(uint64_t table_phys);
    
    // Check if an address is mapped
    bool IsMapped(uint64_t virt);
    
    // Get page flags for an address
    uint32_t GetPageFlags(uint64_t virt);
}
