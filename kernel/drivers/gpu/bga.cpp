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

    // Map 32MB of VRAM (enough for 3 buffers at 1920x1080: ~24MB)
    bool mapped = MMU::MapRange(BGA_VIRT_BASE, this->phys_addr, 
                                32 * 1024 * 1024, 
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
    // Returns Buffer 1 - mapped to userspace for drawing
    // With RAM backbuffer architecture, this is used for VRAM reference
    uint64_t offset = (uint64_t)1 * width * height;
    return this->framebuffer + offset;
}

uint32_t* BGADriver::GetVRAMBuffer() {
    // Returns the VRAM buffer that we copy to during flip
    // Same as GetBackBuffer() in our architecture
    uint64_t offset = (uint64_t)1 * width * height;
    return this->framebuffer + offset;
}

void BGADriver::WaitVSync() {
    // Wait for VSync (Vertical Retrace) with timeout protection
    // VGA Input Status Register 1 (0x3DA): Bit 3 = Vertical Retrace active
    
    uint32_t timeout = 100000; // Approx 1ms at 100MHz cycle speed
    
    // First, wait until we're NOT in retrace (if we caught the end of one)
    while ((IO::inb(0x3DA) & 0x08) && --timeout);
    
    // Now wait until we ARE in retrace (start of blanking interval)
    timeout = 100000;
    while (!(IO::inb(0x3DA) & 0x08) && --timeout);
}

void BGADriver::SwapBuffers() {
    // For copy-based double buffering:
    // 1. VSync wait is called separately via WaitVSync()
    // 2. Copy from RAM to VRAM is done by Graphics::Flip()
    // 3. This function just ensures Buffer 1 is displayed
    
    // Wait for VSync before any display update
    WaitVSync();
    
    // Ensure Buffer 1 is being displayed
    if (displayBuffer != 1) {
        WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, height);
        displayBuffer = 1;
    }
}
