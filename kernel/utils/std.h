#pragma once

#include <stdint.h>
#include <stddef.h>

extern "C" {
    size_t kstrlen(const char* str);
    int kstrcmp(const char* s1, const char* s2);
    int kmemcmp(const void* s1, const void* s2, size_t n);
    void* kmemcpy(void* dest, const void* src, size_t n);
    void* kmemset(void* s, int c, size_t n);

    // libc symbol aliases used by compiler-generated code
    void* memcpy(void* dest, const void* src, size_t n);
    void* memset(void* s, int c, size_t n);
    void* memmove(void* dest, const void* src, size_t n);
    
    // Helper to convert number to string (itoa)
    void kitoa(int value, char* str, int base);
}

