#pragma once

#include <stdint.h>

// Lock-free ring buffer for audio DMA
// Single producer (kernel), single consumer (hardware/DMA)
template<uint32_t SIZE>
class AudioRingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    
private:
    uint8_t buffer[SIZE];
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    
public:
    void Init() {
        write_pos = 0;
        read_pos = 0;
        for (uint32_t i = 0; i < SIZE; i++) buffer[i] = 0;
    }
    
    // Available space for writing
    uint32_t WriteAvailable() const {
        uint32_t w = write_pos;
        uint32_t r = read_pos;
        return SIZE - 1 - ((w - r) & (SIZE - 1));
    }
    
    // Available data for reading
    uint32_t ReadAvailable() const {
        uint32_t w = write_pos;
        uint32_t r = read_pos;
        return (w - r) & (SIZE - 1);
    }
    
    // Write data to buffer (producer)
    uint32_t Write(const uint8_t* data, uint32_t len) {
        uint32_t available = WriteAvailable();
        if (len > available) len = available;
        
        uint32_t w = write_pos & (SIZE - 1);
        for (uint32_t i = 0; i < len; i++) {
            buffer[w] = data[i];
            w = (w + 1) & (SIZE - 1);
        }
        
        // Memory barrier (for DMA coherence)
        __asm__ volatile("" ::: "memory");
        write_pos += len;
        
        return len;
    }
    
    // Read data from buffer (consumer - DMA/IRQ)
    uint32_t Read(uint8_t* out, uint32_t len) {
        uint32_t available = ReadAvailable();
        if (len > available) len = available;
        
        uint32_t r = read_pos & (SIZE - 1);
        for (uint32_t i = 0; i < len; i++) {
            out[i] = buffer[r];
            r = (r + 1) & (SIZE - 1);
        }
        
        __asm__ volatile("" ::: "memory");
        read_pos += len;
        
        return len;
    }
    
    // Get buffer pointer (for DMA setup)
    uint8_t* GetBuffer() { return buffer; }
    uint32_t GetSize() const { return SIZE; }
    
    // Get read position (for DMA)
    uint32_t GetReadPos() const { return read_pos & (SIZE - 1); }
    
    // Advance read position (called by DMA IRQ)
    void AdvanceRead(uint32_t bytes) {
        read_pos += bytes;
    }
    
    // Clear buffer
    void Clear() {
        write_pos = 0;
        read_pos = 0;
    }
    
    bool IsEmpty() const { return ReadAvailable() == 0; }
    bool IsFull() const { return WriteAvailable() == 0; }
};

// Standard audio buffer size (16KB)
#define AUDIO_BUFFER_SIZE 16384
typedef AudioRingBuffer<AUDIO_BUFFER_SIZE> StandardAudioBuffer;
