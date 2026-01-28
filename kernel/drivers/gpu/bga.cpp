#include "bga.h"
#include "../../arch/common/mmu.h"
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/serial/uart.h"
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

  // Map 256MB of VRAM for "Bestial Stability" and 4K triple buffering
  // 1080p triple buffer = ~24MB, 4K triple buffer = ~100MB
  bool mapped =
      MMU::MapRange(BGA_VIRT_BASE, this->phys_addr, 256 * 1024 * 1024,
                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE | PAGE_GLOBAL);

  if (!mapped) {
    Verbose::Error("BGA", "MMU Mapping failed.");
    return false;
  }

  this->framebuffer = (uint32_t *)BGA_VIRT_BASE;
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
  WriteRegister(VBE_DISPI_INDEX_ENABLE,
                VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

  this->displayBuffer = 0;      // Display shows Buffer 0
  this->currentBufferIndex = 1; // Draw to Buffer 1

  UART::Write("[BGA] Mode set: Display=Buffer0, Draw=Buffer1\n");
}

uint32_t *BGADriver::GetBackBuffer() {
  // Returns Buffer 1 - this is where userspace draws
  uint64_t offset = (uint64_t)1 * width * height;
  return this->framebuffer + offset;
}

uint32_t *BGADriver::GetDisplayBuffer() {
  // Returns Buffer 0 - this is what's being displayed
  return this->framebuffer;
}

uint32_t *BGADriver::GetVRAMBuffer() {
  // Alias for GetBackBuffer for compatibility
  return GetBackBuffer();
}

void BGADriver::WaitVSync() {
  // OPTIMIZED: Short timeout to prevent blocking for more than ~2ms
  // At 60Hz, VBlank occurs every ~16.67ms and lasts ~1.3ms
  // We don't want to miss frames by waiting too long

  // Phase 1: Exit current VBlank if we're in one (very short)
  uint32_t timeout = 2000;
  while ((IO::inb(0x3DA) & 0x08) && --timeout) {
    __asm__ volatile("pause");
  }

  // Phase 2: Wait for next VBlank to start
  // Use a shorter timeout - if we miss it, just continue
  // Smooth motion is better than perfect VSync with stutters
  timeout = 50000; // ~1-2ms max wait
  while (!(IO::inb(0x3DA) & 0x08) && --timeout) {
    __asm__ volatile("pause");
  }

  // Note: If timeout expired, we just continue rendering
  // This prevents hangs and keeps the framerate smooth
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
  // P1: Hardware Page Flip Implementation
  // Toggle between Buffer A (0) and Buffer B (1) using Y_OFFSET
  WaitVSync();

  if (this->currentBufferIndex == 1) {
    // We just drew to Buffer 1. Show it.
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, height); // Offset = height
    this->displayBuffer = 1;
    this->currentBufferIndex = 0; // Next frame draw to 0
  } else {
    // We just drew to Buffer 0. Show it.
    WriteRegister(VBE_DISPI_INDEX_Y_OFFSET, 0); // Offset = 0
    this->displayBuffer = 0;
    this->currentBufferIndex = 1; // Next frame draw to 1
  }
}
