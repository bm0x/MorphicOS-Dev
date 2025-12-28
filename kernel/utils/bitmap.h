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

private:
    uint8_t* data;
    size_t size;
};

#endif // BITMAP_H
