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

// Minimal C++ Runtime Support using Kernel Heap Syscalls if available, or just static buffer?
// Userspace has heap?
// sys_sbrk or similar?
// mcl_parser calls new?
// Actually, earlier analysis showed userspace heap might not be fully set up or relies on custom allocator.
// Let's implement a dummy placement new or a simple bump allocator for now if we don't have malloc.
// But wait, desktop uses `new Calculator`. It must have an allocator.
// Checks `userspace/sdk/morphic_syscalls.h` for heap ops.
// No heap ops there.
// How does desktop allocate memory?
// Maybe it doesn't? `new Calculator`?
// `g_calculator` is static global in `desktop.cpp` (Line 51).
// `g_terminal` is static pointer `static Terminal* g_terminal = nullptr;`.
// In explicit initialization?

// If `desktop` doesn't use `new`, that explains why it works.
// But `apps/calculator/main.cpp` uses `new CalculatorApp()`.
// `MorphicAPI::Window* app = new CalculatorApp();`

// I should change `main.cpp` to use stack allocation or static, OR implement `new`.
// Since I don't have a heap manager in userspace yet (unless I port `umm_malloc`), stack is safer.

void* operator new(size_t size) {
    // TODO: Implement userspace malloc via syscall
    // For now, fail or simplistic static buffer?
    // Failing is bad.
    // Let's just use a static buffer for the "one app".
    static uint8_t heap[1024 * 1024]; // 1MB Static Heap
    static size_t offset = 0;
    
    // Align?
    void* ptr = &heap[offset];
    offset += size;
    return ptr;
}

void operator delete(void* p) noexcept {
    // No-op
}

void operator delete(void* p, size_t) noexcept {
    // No-op
}
