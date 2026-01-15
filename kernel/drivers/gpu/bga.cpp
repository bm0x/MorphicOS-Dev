#include "bga.h"
#include "../../arch/common/mmu.h"
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/video/verbose.h"

// Hardcoded safe high-memory virtual address for LFB mapping
// Using 0xFFFF A000 0000 0000 as base
#define BGA_VIRT_BASE 0xFFFFA00000000000ULL

void BGADriver::WriteRegister(uint16_t index, uint16_t value) {
    IO::outw(VBE_DISPI_IOPORT_INDEX, index);
    IO::outw(VBE_DISPI_IOPORT_DATA, value);
}

uint16_t BGADriver::ReadRegister(uint16_t index) {
    IO::outw(VBE_DISPI_IOPORT_INDEX, index);
    return IO::inw(VBE_DISPI_IOPORT_DATA);
}

bool BGADriver::Init() {
    Verbose::Info("BGA", "Probing for device...");
    
    // Check for Device 0x1234:0x1111 (QEMU VGA/BGA)
    HAL::PCIDevice dev;
    if (!HAL::PCI::FindDevice(0x1234, 0x1111, &dev)) {
        Verbose::Info("BGA", "Device not found on PCI bus.");
        return false;
    }
    
    // Check hardware version
    uint16_t id = ReadRegister(VBE_DISPI_INDEX_ID);
    if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5) {
         Verbose::Error("BGA", "Hardware version mismatch (Not BGA compatible).");
         return false;
    }
    
    // Get Linear Framebuffer Address (BAR0)
    // BAR0: Base Address 0. Mask 0xFFFFFFF0 to remove flags.
    uint32_t bar0 = HAL::PCI::GetBAR(dev, 0);
    this->phys_addr = bar0 & 0xFFFFFFF0;
    
    if (this->phys_addr == 0) {
        Verbose::Error("BGA", "Invalid BAR0 address.");
        return false;
    }

    // Map 16MB of VRAM (enough for 3 buffers at high res)
    bool mapped = MMU::MapRange(BGA_VIRT_BASE, this->phys_addr, 
                                16 * 1024 * 1024, 
                                PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE | PAGE_GLOBAL);
                                
    if (!mapped) {
        Verbose::Error("BGA", "MMU Mapping failed.");
        return false;
    }
    
    this->framebuffer = (uint32_t*)BGA_VIRT_BASE;
    Verbose::OK("BGA", "Initialized and VRAM Mapped.");
    return true;
}

void BGADriver::SetMode(int w, int h, int b) {
    this->width = w;
    this->height = h;
    this->bpp = b;
    
    // Disable to configure safely
    WriteRegister(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    
    WriteRegister(VBE_DISPI_INDEX_XRES, width);
    WriteRegister(VBE_DISPI_INDEX_YRES, height);
    WriteRegister(VBE_DISPI_INDEX_BPP, bpp);
    
    // Configure Virtual Screen for Triple Buffering (3x Height)
    WriteRegister(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    WriteRegister(VBE_DISPI_INDEX_VIRT_HEIGHT, height * 3);
    
    WriteRegister(VBE_DISPI_INDEX_X_OFFSET, 0);
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, 0);
    
    // Re-enable with Linear Framebuffer
    WriteRegister(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    
    this->currentBufferIndex = 0;
}

uint32_t* BGADriver::GetBackBuffer() {
    // Return the virtual address of the NEXT buffer (hidden)
    uint32_t drawingIdx = (currentBufferIndex + 1) % 3;
    uint64_t offset = (uint64_t)drawingIdx * width * height; // Offset in 32-bit words if typed pointer? 
    // Wait. Framebuffer is uint32_t*.
    // offset calculation:
    // If width=1024, height=768. 
    // Buffer 0 starts at index 0.
    // Buffer 1 starts at index 1024*768.
    // Correct.
    return this->framebuffer + offset;
}

void BGADriver::SwapBuffers() {
    // Switch visible frame to the one we just drew
    currentBufferIndex = (currentBufferIndex + 1) % 3;
    
    // Hardware Flip via Y Offset
    uint16_t y_offset = currentBufferIndex * height;
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, y_offset);
    
    // Note: In a real driver we would wait for Vertical Retrace here
    // to prevent tearing during the flip. But BGA virtual hardware is instant.
}
