#pragma once

#include <stdint.h>

// Forward declaration
struct IVideoDevice;

// Function pointer types for video operations
typedef void (*VideoPutPixelFunc)(uint32_t x, uint32_t y, uint32_t color);
typedef void (*VideoFillRectFunc)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
typedef void (*VideoBlitFunc)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t* buffer);
typedef void (*VideoClearFunc)(uint32_t color);

// Video Device Interface
// Graphics drivers implement this to register with HAL
struct IVideoDevice {
    char name[32];
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  // Bytes per scanline
    
    // Function pointers (HAL pattern)
    VideoPutPixelFunc put_pixel;
    VideoFillRectFunc fill_rect;
    VideoBlitFunc blit;
    VideoClearFunc clear;
};

// Video Manager - Abstracts framebuffer access
namespace VideoManager {
    void Init();
    
    // Register a video device
    void RegisterDevice(IVideoDevice* device);
    
    // Get primary display
    IVideoDevice* GetPrimary();
    
    // Convenience wrappers (use primary device)
    void PutPixel(uint32_t x, uint32_t y, uint32_t color);
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void Clear(uint32_t color);
    
    uint32_t GetWidth();
    uint32_t GetHeight();
}
