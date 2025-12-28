#pragma once
// Memory Locking for Morphic OS
// Pins critical memory regions to prevent swapping

#include <stdint.h>

namespace Memory {
    
    // Bit 9 in PTE is available for OS use - we use it for "locked" flag
    #define PTE_LOCKED_BIT (1UL << 9)
    
    // Memory lock state tracking
    struct LockedRegion {
        uintptr_t start;
        size_t size;
        bool active;
    };
    
    #define MAX_LOCKED_REGIONS 16
    static LockedRegion lockedRegions[MAX_LOCKED_REGIONS];
    static int lockedCount = 0;
    
    // Lock a memory region (mark as non-swappable)
    // For now this is a logical lock - we just track it
    // Full implementation would modify page table entries
    inline bool Lock(void* addr, size_t size) {
        if (lockedCount >= MAX_LOCKED_REGIONS) return false;
        
        uintptr_t start = (uintptr_t)addr & ~0xFFFUL;  // Page align
        size_t alignedSize = ((size + 0xFFF) & ~0xFFFUL);  // Round up
        
        // Check for overlap with existing locks
        for (int i = 0; i < lockedCount; i++) {
            if (lockedRegions[i].active) {
                uintptr_t end1 = lockedRegions[i].start + lockedRegions[i].size;
                uintptr_t end2 = start + alignedSize;
                if (start < end1 && end2 > lockedRegions[i].start) {
                    // Overlap - extend existing region
                    if (start < lockedRegions[i].start) {
                        lockedRegions[i].size += lockedRegions[i].start - start;
                        lockedRegions[i].start = start;
                    }
                    if (end2 > end1) {
                        lockedRegions[i].size = end2 - lockedRegions[i].start;
                    }
                    return true;
                }
            }
        }
        
        // Add new locked region
        for (int i = 0; i < MAX_LOCKED_REGIONS; i++) {
            if (!lockedRegions[i].active) {
                lockedRegions[i].start = start;
                lockedRegions[i].size = alignedSize;
                lockedRegions[i].active = true;
                lockedCount++;
                return true;
            }
        }
        
        return false;
    }
    
    // Unlock a memory region
    inline bool Unlock(void* addr) {
        uintptr_t target = (uintptr_t)addr & ~0xFFFUL;
        
        for (int i = 0; i < MAX_LOCKED_REGIONS; i++) {
            if (lockedRegions[i].active && lockedRegions[i].start == target) {
                lockedRegions[i].active = false;
                lockedCount--;
                return true;
            }
        }
        return false;
    }
    
    // Check if address is in locked region
    inline bool IsLocked(void* addr) {
        uintptr_t target = (uintptr_t)addr;
        
        for (int i = 0; i < MAX_LOCKED_REGIONS; i++) {
            if (lockedRegions[i].active) {
                uintptr_t end = lockedRegions[i].start + lockedRegions[i].size;
                if (target >= lockedRegions[i].start && target < end) {
                    return true;
                }
            }
        }
        return false;
    }
    
    // Initialize memory lock subsystem
    inline void InitLocks() {
        lockedCount = 0;
        for (int i = 0; i < MAX_LOCKED_REGIONS; i++) {
            lockedRegions[i].active = false;
        }
    }
    
    // Get total locked memory size
    inline size_t GetLockedSize() {
        size_t total = 0;
        for (int i = 0; i < MAX_LOCKED_REGIONS; i++) {
            if (lockedRegions[i].active) {
                total += lockedRegions[i].size;
            }
        }
        return total;
    }
}
