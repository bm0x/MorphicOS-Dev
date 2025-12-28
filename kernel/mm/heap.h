#pragma once

#include <stdint.h>
#include <stddef.h>

namespace KHeap {
    void Init(void* startAddress, size_t size);
    void* Allocate(size_t size);
    void Free(void* ptr);
    
    // Memory info
    size_t GetSize();
    void* GetBase();
    size_t GetUsed();
    size_t GetFree();
}

// Global C-style wrappers
extern "C" {
    void* kmalloc(size_t size);
    void kfree(void* ptr);
}

