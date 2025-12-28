#pragma once

// User-Space Heap Allocator
// Priority-aware memory management for Composer and applications

#include <stdint.h>
#include <stddef.h>

// Memory priority levels
#define MEM_PRIORITY_CRITICAL  0   // Kernel, interrupt handlers
#define MEM_PRIORITY_HIGH      1   // Composer, UI rendering
#define MEM_PRIORITY_NORMAL    2   // User applications
#define MEM_PRIORITY_LOW       3   // Background tasks

// Allocation flags
#define UHEAP_PINNED    (1 << 0)   // Never swap/move
#define UHEAP_ZEROED    (1 << 1)   // Zero-initialize
#define UHEAP_COMPOSER  (1 << 2)   // Composer priority

// User heap configuration
#define UHEAP_BASE          0x10000000  // 256 MB mark
#define UHEAP_INITIAL_SIZE  (4 * 1024 * 1024)   // 4 MB initial
#define UHEAP_MAX_SIZE      (256 * 1024 * 1024) // 256 MB max
#define UHEAP_EXPAND_SIZE   (1 * 1024 * 1024)   // 1 MB per expansion

// Allocation region header
struct UHeapBlock {
    uint32_t magic;         // 0x55484550 = "UHEP"
    uint32_t size;          // Size of allocation (including header)
    uint8_t  priority;      // Memory priority
    uint8_t  flags;         // Allocation flags
    uint16_t reserved;
    UHeapBlock* next;       // Next block in free list
    UHeapBlock* prev;       // Previous block
};

#define UHEAP_MAGIC 0x55484550

// Memory statistics
struct UHeapStats {
    size_t total_size;      // Total heap size
    size_t used_size;       // Currently allocated
    size_t free_size;       // Available
    size_t high_priority;   // High priority allocations
    size_t expansion_count; // Number of expansions
    size_t alloc_count;     // Total allocations
    size_t free_count;      // Total frees
};

namespace UserHeap {
    // Initialize user heap
    void Init();
    
    // Allocate memory with priority
    void* Allocate(size_t size, uint8_t priority = MEM_PRIORITY_NORMAL, uint8_t flags = 0);
    
    // Free memory
    void Free(void* ptr);
    
    // Reallocate (resize)
    void* Realloc(void* ptr, size_t new_size);
    
    // Expand heap by requesting pages from PMM
    bool Expand(size_t bytes);
    
    // Set priority on existing allocation
    bool SetPriority(void* ptr, uint8_t priority);
    
    // Pin allocation (prevent GC/swap)
    bool Pin(void* ptr);
    bool Unpin(void* ptr);
    
    // Get allocation info
    bool GetBlockInfo(void* ptr, UHeapBlock* info);
    
    // Get heap statistics
    void GetStats(UHeapStats* stats);
    
    // Compact heap (defragment)
    void Compact();
    
    // Release low-priority memory under pressure
    size_t ReclaimMemory(size_t target, uint8_t max_priority);
    
    // Check if address is in user heap
    bool IsUserAddress(void* ptr);
    
    // Debug: dump heap state
    void DumpHeap();
}

// Convenience macros for Composer
#define ualloc(size)         UserHeap::Allocate(size, MEM_PRIORITY_NORMAL)
#define ualloc_high(size)    UserHeap::Allocate(size, MEM_PRIORITY_HIGH, UHEAP_PINNED)
#define ualloc_composer(size) UserHeap::Allocate(size, MEM_PRIORITY_HIGH, UHEAP_COMPOSER | UHEAP_PINNED)
#define ufree(ptr)           UserHeap::Free(ptr)
