#include <stdint.h>
#include <stddef.h>
#include "morphic_syscalls.h"

extern "C" {
    void __cxa_pure_virtual() {
        // sys_panic("Pure virtual function call");
        while (1);
    }

    // Weak main fallback or let app define it?
    // App defines main.
}

// ============================================================================
// Userspace Heap Allocator with Free-List
// ============================================================================
// Implements a proper allocator with allocation and deallocation support.
// Uses a linked free-list with coalescing to prevent memory fragmentation.
// This fixes the critical memory leak that caused crashes when opening apps.

namespace {
    constexpr size_t HEAP_SIZE = 2 * 1024 * 1024;  // 2MB per app (increased)
    constexpr size_t MIN_BLOCK = 32;                // Minimum allocation
    constexpr size_t ALIGNMENT = 16;                // 16-byte alignment for SIMD
    
    // Block header (16 bytes, keeps alignment)
    struct BlockHeader {
        size_t size;          // Size including header
        BlockHeader* next;    // Next free block (only valid when free)
        uint32_t magic;       // 0xDEADBEEF when allocated, 0xFEEDFACE when free
        uint32_t padding;     // Alignment padding
    };
    
    static uint8_t heap_storage[HEAP_SIZE] __attribute__((aligned(16)));
    static BlockHeader* freeList = nullptr;
    static bool initialized = false;
    
    inline void InitHeap() {
        if (initialized) return;
        
        // Initialize entire heap as one free block
        freeList = reinterpret_cast<BlockHeader*>(heap_storage);
        freeList->size = HEAP_SIZE;
        freeList->next = nullptr;
        freeList->magic = 0xFEEDFACE;
        initialized = true;
    }
    
    // Coalesce adjacent free blocks
    inline void Coalesce() {
        BlockHeader* current = freeList;
        while (current && current->next) {
            uint8_t* currentEnd = reinterpret_cast<uint8_t*>(current) + current->size;
            if (currentEnd == reinterpret_cast<uint8_t*>(current->next)) {
                // Merge with next block
                current->size += current->next->size;
                current->next = current->next->next;
            } else {
                current = current->next;
            }
        }
    }
}

void* operator new(size_t size) {
    InitHeap();
    
    // Calculate total size needed (header + data, aligned)
    size_t totalSize = sizeof(BlockHeader) + ((size + ALIGNMENT - 1) & ~(ALIGNMENT - 1));
    if (totalSize < MIN_BLOCK) totalSize = MIN_BLOCK;
    
    // First-fit search through free list
    BlockHeader* prev = nullptr;
    BlockHeader* current = freeList;
    
    while (current) {
        if (current->size >= totalSize) {
            // Found a suitable block
            if (current->size >= totalSize + MIN_BLOCK) {
                // Split block if remaining is large enough
                BlockHeader* newBlock = reinterpret_cast<BlockHeader*>(
                    reinterpret_cast<uint8_t*>(current) + totalSize
                );
                newBlock->size = current->size - totalSize;
                newBlock->next = current->next;
                newBlock->magic = 0xFEEDFACE;
                
                current->size = totalSize;
                
                // Update free list
                if (prev) {
                    prev->next = newBlock;
                } else {
                    freeList = newBlock;
                }
            } else {
                // Use entire block
                if (prev) {
                    prev->next = current->next;
                } else {
                    freeList = current->next;
                }
            }
            
            current->magic = 0xDEADBEEF;  // Mark as allocated
            current->next = nullptr;
            
            // Return pointer after header
            return reinterpret_cast<uint8_t*>(current) + sizeof(BlockHeader);
        }
        
        prev = current;
        current = current->next;
    }
    
    // Out of memory - should call sys_panic but we're in userspace
    return nullptr;
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    
    // Get block header
    BlockHeader* block = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<uint8_t*>(ptr) - sizeof(BlockHeader)
    );
    
    // Validate magic (detect double-free or corruption)
    if (block->magic != 0xDEADBEEF) return;
    
    // Mark as free and add to free list (sorted by address for coalescing)
    block->magic = 0xFEEDFACE;
    
    // Insert into sorted position
    BlockHeader* prev = nullptr;
    BlockHeader* current = freeList;
    
    while (current && current < block) {
        prev = current;
        current = current->next;
    }
    
    block->next = current;
    if (prev) {
        prev->next = block;
    } else {
        freeList = block;
    }
    
    // Coalesce adjacent blocks
    Coalesce();
}

void operator delete(void* ptr, size_t) noexcept {
    operator delete(ptr);  // Delegate to standard delete
}

extern "C" {
    void user_early_trace_start() {
        const char* msg = "[userspace] early: start\n";
        sys_debug_print(msg);
    }

    void user_early_trace_end() {
        const char* msg = "[userspace] early: main returned\n";
        sys_debug_print(msg);
    }
}
