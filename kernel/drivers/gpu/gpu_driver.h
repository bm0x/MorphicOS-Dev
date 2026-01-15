#pragma once
#include <stdint.h>

class IGPUDriver {
public:
    virtual ~IGPUDriver() {}
    
    // Initialize the hardware. Returns true if successful.
    virtual bool Init() = 0;
    
    // Set video mode (e.g. 1024, 768, 32)
    virtual void SetMode(int width, int height, int bpp) = 0;
    
    // Triple Buffering: Swap the visible buffer to the next ready frame
    virtual void SwapBuffers() = 0;
    
    // Get the address of the *current* backbuffer (where we should draw NEXT)
    virtual uint32_t* GetBackBuffer() = 0;
    
    virtual const char* GetName() = 0;
};
