#include "std.h"

extern "C" {
    // --- C++ ABI Support ---
    
    // Pure virtual function call handler
    void __cxa_pure_virtual() {
        // We can't print easily here without dependency, so just hang
        while(1);
    }

    // Static Object Initialization Guards (for local statics)
    int __cxa_guard_acquire(uint64_t *guard) {
        if (*((volatile uint8_t *)guard) == 0) {
            return 1; // Need to initialize
        }
        return 0; // Already initialized
    }

    void __cxa_guard_release(uint64_t *guard) {
        *((volatile uint8_t *)guard) = 1; // Mark initialized
    }

    void __cxa_guard_abort(uint64_t *guard) {
        // Reset if initialization failed (shouldn't happen)
        *((volatile uint8_t *)guard) = 0;
    }

    size_t kstrlen(const char* str) {
        size_t len = 0;
        while (str[len]) len++;
        return len;
    }

    int kstrcmp(const char* s1, const char* s2) {
        while (*s1 && (*s1 == *s2)) {
            s1++;
            s2++;
        }
        return *(const unsigned char*)s1 - *(const unsigned char*)s2;
    }

    int kstricmp(const char* s1, const char* s2) {
        while (*s1 && *s2) {
            char c1 = *s1;
            char c2 = *s2;
            // Convert to lowercase
            if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
            if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
            if (c1 != c2) return c1 - c2;
            s1++;
            s2++;
        }
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        return c1 - c2;
    }

    int kmemcmp(const void* s1, const void* s2, size_t n) {
        const unsigned char* p1 = (const unsigned char*)s1;
        const unsigned char* p2 = (const unsigned char*)s2;
        for (size_t i = 0; i < n; i++) {
            if (p1[i] != p2[i]) return p1[i] - p2[i];
        }
        return 0;
    }

    void* kmemcpy(void* dest, const void* src, size_t n) {
        char* d = (char*)dest;
        const char* s = (const char*)src;
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
        return dest;
    }

    void* kmemset(void* s, int c, size_t n) {
        unsigned char* p = (unsigned char*)s;
        while (n--) {
            *p++ = (unsigned char)c;
        }
        return s;
    }

    void* memcpy(void* dest, const void* src, size_t n) {
        return kmemcpy(dest, src, n);
    }

    void* memset(void* s, int c, size_t n) {
        return kmemset(s, c, n);
    }

    void* memmove(void* dest, const void* src, size_t n) {
        // Safe overlap handling
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        if (d == s || n == 0) return dest;
        if (d < s) {
            for (size_t i = 0; i < n; i++) d[i] = s[i];
        } else {
            for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
        }
        return dest;
    }

    void kitoa(int value, char* str, int base) {
        if (base < 2 || base > 36) {
            *str = '\0';
            return;
        }

        char* ptr = str, *ptr1 = str, tmp_char;
        int tmp_value;

        if (value < 0 && base == 10) {
            *ptr++ = '-';
            ptr1++; // Start reversing after sign
            value = -value;
        } else if (value < 0) {
             // Treat as unsigned for non-decimal? 
             // For simple kitoa, usually we handle unsigned separately. 
             // Let's assume positive for hex/others or cast.
             // But for now, simple absolute value.
             value = -value; // Just abs it for non-10 bases if fed negative?
             // Or better, just handle typical int cases.
        }

        do {
            tmp_value = value;
            value /= base;
            *ptr++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[tmp_value - (value * base)];
        } while (value);

        *ptr-- = '\0';

        // Reverse string
        while (ptr1 < ptr) {
            tmp_char = *ptr;
            *ptr-- = *ptr1;
            *ptr1++ = tmp_char;
        }
    }
}
