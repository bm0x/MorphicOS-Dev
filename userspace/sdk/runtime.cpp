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

// Minimal C++ Runtime Support
// Using static buffer allocator for userspace apps.
// NOTE: sys_malloc returns kernel heap addresses which are NOT mapped to userspace,
// so we cannot use syscalls for memory allocation in userspace apps.

void* operator new(size_t size) {
    static uint8_t heap[1024 * 1024]; // 1MB Static Heap per app
    static size_t offset = 0;
    
    // Align to 16 bytes
    size = (size + 15) & ~15;
    
    void* ptr = &heap[offset];
    offset += size;
    return ptr;
}

void operator delete(void* p) noexcept {
    // No-op for static allocator
}

void operator delete(void* p, size_t) noexcept {
    // No-op for static allocator
}
