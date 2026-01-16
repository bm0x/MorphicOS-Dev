// User-Space Heap Allocator Implementation
// Priority-aware allocation with dynamic expansion

#include "user_heap.h"
#include "heap.h"
#include "pmm.h"
#include "../arch/common/spinlock.h"
#include "../hal/video/early_term.h"
#include "../utils/std.h"

namespace UserHeap {
    // Heap state
    static uint8_t* heapBase = nullptr;
    static size_t heapSize = 0;
    static size_t heapUsed = 0;
    static UHeapBlock* freeList = nullptr;
    static Spinlock heapLock = SPINLOCK_INIT;
    
    // Statistics
    static UHeapStats stats = {0};
    
    void Init() {
        CRITICAL_SECTION(heapLock);
        
        // Allocate initial heap from PMM (P2 Optimization)
        // PMM::AllocContiguous returns a physical address. 
        // We assume 1:1 mapping or Identity Map for kernel access here.
        // Or if Identity map covers low memory, we are good.
        size_t pages = (UHEAP_INITIAL_SIZE + 4095) / 4096;
        heapBase = (uint8_t*)PMM::AllocContiguous(pages);
        if (!heapBase) {
            EarlyTerm::Print("[UserHeap] Failed to allocate initial heap!\n");
            return;
        }
        
        heapSize = UHEAP_INITIAL_SIZE;
        heapUsed = 0;
        
        // Initialize first free block
        freeList = (UHeapBlock*)heapBase;
        freeList->magic = UHEAP_MAGIC;
        freeList->size = heapSize;
        freeList->priority = MEM_PRIORITY_LOW;
        freeList->flags = 0;
        freeList->next = nullptr;
        freeList->prev = nullptr;
        
        // Update stats
        stats.total_size = heapSize;
        stats.free_size = heapSize - sizeof(UHeapBlock);
        stats.used_size = 0;
        
        EarlyTerm::Print("[UserHeap] Initialized ");
        EarlyTerm::PrintDec(heapSize / 1024);
        EarlyTerm::Print(" KB at 0x");
        EarlyTerm::PrintHex((uint64_t)heapBase);
        EarlyTerm::Print("\n");
    }
    
    void* Allocate(size_t size, uint8_t priority, uint8_t flags) {
        if (size == 0) return nullptr;
        
        CRITICAL_SECTION(heapLock);
        
        // Align to 16 bytes
        size = (size + 15) & ~15;
        size_t totalSize = size + sizeof(UHeapBlock);
        
        // Find suitable free block (first-fit)
        UHeapBlock* block = freeList;
        UHeapBlock* best = nullptr;
        
        while (block) {
            if (block->size >= totalSize) {
                // Prefer exact fit or close
                if (!best || block->size < best->size) {
                    best = block;
                    if (block->size == totalSize) break;
                }
            }
            block = block->next;
        }
        
        if (!best) {
            // Try to expand heap
            if (!Expand(totalSize > UHEAP_EXPAND_SIZE ? totalSize : UHEAP_EXPAND_SIZE)) {
                return nullptr;
            }
            // Retry after expansion
            return Allocate(size, priority, flags);
        }
        
        // Remove from free list
        if (best->prev) best->prev->next = best->next;
        if (best->next) best->next->prev = best->prev;
        if (freeList == best) freeList = best->next;
        
        // Split if much larger
        if (best->size > totalSize + sizeof(UHeapBlock) + 32) {
            UHeapBlock* remainder = (UHeapBlock*)((uint8_t*)best + totalSize);
            remainder->magic = UHEAP_MAGIC;
            remainder->size = best->size - totalSize;
            remainder->priority = MEM_PRIORITY_LOW;
            remainder->flags = 0;
            remainder->next = freeList;
            remainder->prev = nullptr;
            if (freeList) freeList->prev = remainder;
            freeList = remainder;
            
            best->size = totalSize;
        }
        
        // Setup allocated block
        best->magic = UHEAP_MAGIC;
        best->priority = priority;
        best->flags = flags;
        best->next = nullptr;
        best->prev = nullptr;
        
        // Update stats
        heapUsed += best->size;
        stats.used_size = heapUsed;
        stats.free_size = heapSize - heapUsed;
        stats.alloc_count++;
        if (priority <= MEM_PRIORITY_HIGH) {
            stats.high_priority += best->size;
        }
        
        void* ptr = (uint8_t*)best + sizeof(UHeapBlock);
        
        // Zero if requested
        if (flags & UHEAP_ZEROED) {
            kmemset(ptr, 0, size);
        }
        
        return ptr;
    }
    
    void Free(void* ptr) {
        if (!ptr) return;
        
        CRITICAL_SECTION(heapLock);
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        
        // Validate
        if (block->magic != UHEAP_MAGIC) {
            EarlyTerm::Print("[UserHeap] Invalid free!\n");
            return;
        }
        
        // Update stats
        heapUsed -= block->size;
        stats.used_size = heapUsed;
        stats.free_size = heapSize - heapUsed;
        stats.free_count++;
        if (block->priority <= MEM_PRIORITY_HIGH) {
            stats.high_priority -= block->size;
        }
        
        // Add to free list (Sorted Insert for Coalescing)
        // Find insert position
        UHeapBlock* current = freeList;
        UHeapBlock* prev = nullptr;
        
        while (current && current < block) {
            prev = current;
            current = current->next;
        }
        
        // Insert 'block' between 'prev' and 'current'
        block->next = current;
        block->prev = prev;
        
        if (prev) prev->next = block;
        else freeList = block;
        
        if (current) current->prev = block;
        
        // Coalesce with Next (current)
        if (current) {
            // Check adjacency: (uint8_t*)block + sizeof(UHeapBlock) + block->size == (uint8_t*)current
            uint8_t* blockEnd = (uint8_t*)block + sizeof(UHeapBlock) + block->size;
            if (blockEnd == (uint8_t*)current) {
                // Merge
                block->size += sizeof(UHeapBlock) + current->size;
                block->next = current->next;
                if (current->next) current->next->prev = block;
                // 'current' is now gone
            }
        }
        
        // Coalesce with Prev
        if (prev) {
            uint8_t* prevEnd = (uint8_t*)prev + sizeof(UHeapBlock) + prev->size;
            if (prevEnd == (uint8_t*)block) {
                // Merge
                prev->size += sizeof(UHeapBlock) + block->size;
                prev->next = block->next;
                if (block->next) block->next->prev = prev;
                // 'block' is now gone
            }
        }
    }
    
    void* Realloc(void* ptr, size_t new_size) {
        if (!ptr) return Allocate(new_size);
        if (new_size == 0) {
            Free(ptr);
            return nullptr;
        }
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        size_t old_size = block->size - sizeof(UHeapBlock);
        
        if (new_size <= old_size) return ptr;
        
        void* new_ptr = Allocate(new_size, block->priority, block->flags);
        if (!new_ptr) return nullptr;
        
        kmemcpy(new_ptr, ptr, old_size);
        Free(ptr);
        
        return new_ptr;
    }
    
    bool Expand(size_t bytes) {
        CRITICAL_SECTION(heapLock);
        
        if (heapSize + bytes > UHEAP_MAX_SIZE) {
            return false;
        }
        
        // For now, allocate from kernel heap
        // In real implementation, would request pages from PMM
        void* newMem = kmalloc(bytes);
        if (!newMem) return false;
        
        // Add as free block
        UHeapBlock* newBlock = (UHeapBlock*)newMem;
        newBlock->magic = UHEAP_MAGIC;
        newBlock->size = bytes;
        newBlock->priority = MEM_PRIORITY_LOW;
        newBlock->flags = 0;
        newBlock->next = freeList;
        newBlock->prev = nullptr;
        if (freeList) freeList->prev = newBlock;
        freeList = newBlock;
        
        heapSize += bytes;
        stats.total_size = heapSize;
        stats.free_size += bytes;
        stats.expansion_count++;
        
        EarlyTerm::Print("[UserHeap] Expanded by ");
        EarlyTerm::PrintDec(bytes / 1024);
        EarlyTerm::Print(" KB\n");
        
        return true;
    }
    
    bool SetPriority(void* ptr, uint8_t priority) {
        if (!ptr) return false;
        
        CRITICAL_SECTION(heapLock);
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        if (block->magic != UHEAP_MAGIC) return false;
        
        block->priority = priority;
        return true;
    }
    
    bool Pin(void* ptr) {
        if (!ptr) return false;
        
        CRITICAL_SECTION(heapLock);
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        if (block->magic != UHEAP_MAGIC) return false;
        
        block->flags |= UHEAP_PINNED;
        return true;
    }
    
    bool Unpin(void* ptr) {
        if (!ptr) return false;
        
        CRITICAL_SECTION(heapLock);
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        if (block->magic != UHEAP_MAGIC) return false;
        
        block->flags &= ~UHEAP_PINNED;
        return true;
    }
    
    bool GetBlockInfo(void* ptr, UHeapBlock* info) {
        if (!ptr || !info) return false;
        
        UHeapBlock* block = (UHeapBlock*)((uint8_t*)ptr - sizeof(UHeapBlock));
        if (block->magic != UHEAP_MAGIC) return false;
        
        kmemcpy(info, block, sizeof(UHeapBlock));
        return true;
    }
    
    void GetStats(UHeapStats* out) {
        if (!out) return;
        kmemcpy(out, &stats, sizeof(UHeapStats));
    }
    
    size_t ReclaimMemory(size_t target, uint8_t max_priority) {
        // TODO: Implement memory reclamation for low-priority blocks
        (void)target;
        (void)max_priority;
        return 0;
    }
    
    void Compact() {
        // TODO: Implement heap defragmentation
    }
    
    bool IsUserAddress(void* ptr) {
        return ptr >= heapBase && ptr < heapBase + heapSize;
    }
    
    void DumpHeap() {
        EarlyTerm::Print("=== User Heap Status ===\n");
        EarlyTerm::Print("Total: ");
        EarlyTerm::PrintDec(stats.total_size / 1024);
        EarlyTerm::Print(" KB\nUsed: ");
        EarlyTerm::PrintDec(stats.used_size / 1024);
        EarlyTerm::Print(" KB\nFree: ");
        EarlyTerm::PrintDec(stats.free_size / 1024);
        EarlyTerm::Print(" KB\nHigh-Priority: ");
        EarlyTerm::PrintDec(stats.high_priority / 1024);
        EarlyTerm::Print(" KB\nExpansions: ");
        EarlyTerm::PrintDec(stats.expansion_count);
        EarlyTerm::Print("\n");
    }
}
