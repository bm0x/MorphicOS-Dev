#include "bga.h"
#include "../../arch/common/mmu.h"
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/video/verbose.h"
#include "../../hal/serial/uart.h"

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

    // Map 128MB of VRAM for "Extreme Stability" and 4K support
    // 1080p triple buffer = ~24MB, 4K triple buffer = ~100MB
    bool mapped = MMU::MapRange(BGA_VIRT_BASE, this->phys_addr, 
                                128 * 1024 * 1024, 
                                PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE | PAGE_GLOBAL);
                                
    if (!mapped) {
        Verbose::Error("BGA", "MMU Mapping failed.");
        return false;
    }
    
    this->framebuffer = (uint32_t*)BGA_VIRT_BASE;
    Verbose::OK("BGA", "Initialized with 128MB VRAM mapped.");
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
    
    // Display starts at Buffer 0 (Y_OFFSET = 0)
    WriteRegister(VBE_DISPI_INDEX_X_OFFSET, 0);
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, 0);
    
    // Re-enable with Linear Framebuffer
    WriteRegister(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    
    this->displayBuffer = 0;  // Display shows Buffer 0
    this->currentBufferIndex = 1;  // Draw to Buffer 1
    
    UART::Write("[BGA] Mode set: Display=Buffer0, Draw=Buffer1\n");
}

uint32_t* BGADriver::GetBackBuffer() {
    // Returns Buffer 1 - this is where userspace draws
    uint64_t offset = (uint64_t)1 * width * height;
    return this->framebuffer + offset;
}

uint32_t* BGADriver::GetDisplayBuffer() {
    // Returns Buffer 0 - this is what's being displayed
    return this->framebuffer;
}

uint32_t* BGADriver::GetVRAMBuffer() {
    // Alias for GetBackBuffer for compatibility
    return GetBackBuffer();
}

void BGADriver::WaitVSync() {
    // Wait for VSync (Vertical Retrace) with timeout protection
    // VGA Input Status Register 1 (0x3DA): Bit 3 = Vertical Retrace active
    
    uint32_t timeout = 200000; // Increased timeout for safety
    
    // First, wait until we're NOT in retrace (if we caught the end of one)
    while ((IO::inb(0x3DA) & 0x08) && --timeout);
    
    // Now wait until we ARE in retrace (start of blanking interval)
    timeout = 200000;
    while (!(IO::inb(0x3DA) & 0x08) && --timeout);
}

void BGADriver::CopyBufferToDisplay() {
    // DISABLED - causing crashes
    // Copy from Back Buffer (1) to Display Buffer (0)
    // Left here for future debugging
}

void BGADriver::SetDisplayToBuffer1() {
    // Set Y_OFFSET to show Buffer 1 (at offset height)
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, height);
    displayBuffer = 1;
    UART::Write("[BGA] Display switched to Buffer 1\n");
}

void BGADriver::SwapBuffers() {
    // SIMPLIFIED: VSync wait only - no buffer copy
    // Drawing happens directly to Buffer 1 which is always displayed
    WaitVSync();
}
