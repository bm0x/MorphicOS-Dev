#include "heap.h"
#include "../hal/video/early_term.h" // For panic debug if needed
#include "../arch/common/spinlock.h"

struct HeapSegmentHeader {
    size_t length;
    HeapSegmentHeader* next;
    HeapSegmentHeader* prev;
    bool free;
    uint32_t magic; // Integrity check
};

// Heap lock for thread-safety
static Spinlock heapLock = SPINLOCK_INIT;

namespace KHeap {

    HeapSegmentHeader* firstSegment;
    void* heapStart;
    size_t heapSize;

    void Init(void* startAddress, size_t size) {
        heapStart = startAddress;
        heapSize = size;
        
        // Initial monolithic segment
        firstSegment = (HeapSegmentHeader*)startAddress;
        firstSegment->length = size - sizeof(HeapSegmentHeader);
        firstSegment->next = nullptr;
        firstSegment->prev = nullptr;
        firstSegment->free = true;
        firstSegment->magic = 0xC0FFEE;
        
        EarlyTerm::Print("[KHeap] Initialized at ");
        EarlyTerm::PrintHex((uint64_t)startAddress);
        EarlyTerm::Print("\n");
    }

    void* Allocate(size_t size) {
        CRITICAL_SECTION(heapLock);
        
        // Align size to 16 bytes?
        size = (size + 15) & ~15;
        
        if (size == 0) return nullptr;

        HeapSegmentHeader* current = firstSegment;
        while (current) {
            if (current->free) {
                if (current->length == size) {
                    current->free = false;
                    return (void*)(current + 1);
                }
                
                if (current->length > (size + sizeof(HeapSegmentHeader))) {
                    // Split
                    HeapSegmentHeader* newSegment = (HeapSegmentHeader*)((uint64_t)current + sizeof(HeapSegmentHeader) + size);
                    newSegment->length = current->length - size - sizeof(HeapSegmentHeader);
                    newSegment->free = true;
                    newSegment->magic = 0xC0FFEE;
                    newSegment->next = current->next;
                    newSegment->prev = current;
                    
                    if (current->next) {
                        current->next->prev = newSegment;
                    }
                    
                    current->length = size;
                    current->next = newSegment;
                    current->free = false;
                    
                    return (void*)(current + 1);
                }
            }
            current = current->next;
        }
        return nullptr; // OOM
    }

    void CombineFreeSegments(HeapSegmentHeader* a, HeapSegmentHeader* b) {
        if (!a || !b) return;
        if (!a->free || !b->free) return;
        if (a->next != b) return;
        
        a->length += b->length + sizeof(HeapSegmentHeader);
        a->next = b->next;
        if (b->next) {
            b->next->prev = a;
        }
    }

    void Free(void* ptr) {
        CRITICAL_SECTION(heapLock);
        
        if (!ptr) return;
        
        HeapSegmentHeader* header = (HeapSegmentHeader*)ptr - 1;
        if (header->magic != 0xC0FFEE) {
            EarlyTerm::Print("[KHeap] Corruption Detected in Free!\n");
            return; 
        }
        
        header->free = true;
        
        // Coalesce right
        if (header->next && header->next->free) {
            CombineFreeSegments(header, header->next);
        }
        
        // Coalesce left
        if (header->prev && header->prev->free) {
            CombineFreeSegments(header->prev, header);
        }
    }
    
    size_t GetSize() { return heapSize; }
    void* GetBase() { return heapStart; }
    
    size_t GetFree() {
        size_t free = 0;
        HeapSegmentHeader* current = firstSegment;
        while (current) {
            if (current->free) free += current->length;
            current = current->next;
        }
        return free;
    }
    
    size_t GetUsed() {
        return heapSize - GetFree() - sizeof(HeapSegmentHeader);
    }
}

extern "C" {
    void* kmalloc(size_t size) {
        return KHeap::Allocate(size);
    }
    
    void kfree(void* ptr) {
        KHeap::Free(ptr);
    }
}

// C++ Operators (Must be global C++)
void* operator new(size_t size) {
    return kmalloc(size);
}

void* operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void* p) {
    kfree(p);
}

void operator delete[](void* p) {
    kfree(p);
}

void operator delete(void* p, size_t s) {
    kfree(p);
}

void operator delete[](void* p, size_t s) {
    kfree(p);
}
