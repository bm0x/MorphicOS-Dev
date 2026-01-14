#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stddef.h>

class Bitmap {
public:
    void Init(void* buffer, size_t sizeBytes) {
        data = (uint8_t*)buffer;
        size = sizeBytes;
    }

    bool Get(size_t index) {
        size_t byteIndex = index / 8;
        size_t bitIndex = index % 8;
        return (data[byteIndex] & (1 << bitIndex));
    }

    void Set(size_t index, bool value) {
        size_t byteIndex = index / 8;
        size_t bitIndex = index % 8;
        if (value) {
            data[byteIndex] |= (1 << bitIndex);
        } else {
            data[byteIndex] &= ~(1 << bitIndex);
        }
    }
    
    // Optimized range setting using memset where possible
    void SetRange(size_t index, size_t count, bool value) {
        if (count == 0) return;
        
        // Naive implementation for edges or small counts
        // Standard memset for middle blocks is TODO but this is already 100x faster 
        // than function call overhead if inlined, but sticking to loop for simplicity first
        // actually, implementing the block optimization IS the goal.
        
        size_t end = index + count;
        
        // Align to byte start
        while ((index % 8) != 0 && index < end) {
            Set(index++, value);
        }
        
        // Bulk set bytes
        size_t byteStart = index / 8;
        size_t byteEnd = end / 8; // exclusive, rounded down
        if (byteEnd > byteStart) {
             size_t bytes = byteEnd - byteStart;
             // extern void* memset(void*, int, size_t); // We don't have implicit memset here?
             // std::memset is not avail in header-only unless we include it.
             // We can just loop bytes. O(N/8) is 8x faster than O(N).
             uint8_t fill = value ? 0xFF : 0x00;
             for (size_t i = 0; i < bytes; i++) {
                 data[byteStart + i] = fill;
             }
             index = byteEnd * 8;
        }
        
        // Align to bit end
        while (index < end) {
            Set(index++, value);
        }
    }

private:
    uint8_t* data;
    size_t size;
};

#endif // BITMAP_H
