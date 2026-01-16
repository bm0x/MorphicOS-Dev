#pragma once
#include "gpu_driver.h"
#include "../../hal/arch/x86_64/pci.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

#define VBE_DISPI_ID0               0xB0C0
#define VBE_DISPI_ID1               0xB0C1
#define VBE_DISPI_ID2               0xB0C2
#define VBE_DISPI_ID3               0xB0C3
#define VBE_DISPI_ID4               0xB0C4
#define VBE_DISPI_ID5               0xB0C5

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_GETCAPS           0x02
#define VBE_DISPI_8BIT_DAC          0x20
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

class BGADriver : public IGPUDriver {
private:
    uint32_t width, height, bpp;
    uint32_t* framebuffer; // Virtual address
    uint32_t currentBufferIndex;
    uint32_t displayBuffer;   // Which buffer is currently being displayed (0 or 1)
    uint64_t phys_addr;
    
public:
    BGADriver() : framebuffer(nullptr), currentBufferIndex(0), displayBuffer(0), phys_addr(0) {}
    
    bool Init() override;
    void SetMode(int w, int h, int b) override;
    void SwapBuffers() override;
    uint32_t* GetBackBuffer() override;
    const char* GetName() override { return "Bochs Graphics Adapter"; }
    
    // Double Buffering Support
    void WaitVSync();                    // Wait for vertical retrace
    uint32_t* GetVRAMBuffer();           // Get VRAM buffer for kernel copy target
    uint32_t* GetDisplayBuffer();        // Get display buffer (Buffer 0)
    void CopyBufferToDisplay();          // Copy back buffer → display buffer
    void SetDisplayToBuffer1();          // Switch display to show Buffer 1
    
    // Internal
    void WriteRegister(uint16_t index, uint16_t value);
    uint16_t ReadRegister(uint16_t index);
};
