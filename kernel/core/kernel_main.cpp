#include "../../shared/boot_info.h"
#include "../hal/video/early_term.h"
#include "../hal/video/graphics.h"
#include "../hal/video/verbose.h"
#include "../hal/video/compositor.h"
#include "../hal/video/font_renderer.h"
#include "../hal/device_registry.h"
#include "../hal/input/mouse.h"
#include "../hal/input/keymap.h"
#include "../hal/audio/audio_device.h"
#include "../hal/audio/mixer.h"
#include "../hal/serial/uart.h"
#include "../mm/pmm.h"
#include "../mm/write_combining.h"
#include "../hal/arch/x86_64/gdt.h"
#include "../hal/arch/x86_64/idt.h"
#include "../hal/arch/x86_64/pic.h"
#include "../hal/arch/x86_64/tss.h"
#include "../hal/arch/x86_64/syscall.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../fs/initrd.h"
#include "shell.h"
#include "bootconfig.h"
#include "../process/scheduler.h"
#include "../utils/std.h"


// Driver Headers
namespace PIT { void Init(uint32_t); }
namespace Keyboard { void Init(); char GetChar(); }
namespace RAMDisk { void Init(); }

// Kernel stack for TSS (4KB aligned)
static uint8_t kernel_tss_stack[4096] __attribute__((aligned(16)));

// Store framebuffer for later use
static FramebufferInfo* gFramebuffer = nullptr;

void BackgroundTask() {
    uint64_t ticks = 0;
    char buffer[32];
    
    while(1) {
        for(volatile int i=0; i<50000000; i++);
        
        ticks++;
        kmemset(buffer, 0, 32);
        kitoa(ticks, buffer, 10);
        
        EarlyTerm::PrintAt(70, 0, "Swift: ");
        EarlyTerm::PrintAt(77, 0, buffer);
    }
}

extern "C" void kernel_main(BootInfo* bootInfo) {
    // 0. Validate BootInfo
    if (!bootInfo || bootInfo->magic != 0xDEADBEEFCAFEBABE) {
        while(1) __asm__("hlt");
    }

    // 1. Initialize Display
    EarlyTerm::Init(&bootInfo->framebuffer);
    gFramebuffer = &bootInfo->framebuffer;
    
    // 2. Core Hardware Init
    GDT::Init();
    IDT::Init();
    PIC::Remap();
    PMM::Init(bootInfo);

    EarlyTerm::Print("[OK] Core Systems Validated.\n");

    // 3. Initialize Heap dynamically based on available RAM
    size_t freeRAM = PMM::GetFreeMemory();
    size_t heapSize = (freeRAM * 3) / 4;  // Use 75% of free RAM
    
    // Minimum 32MB, no maximum limit
    if (heapSize < 32 * 1024 * 1024) heapSize = 32 * 1024 * 1024;
    
    KHeap::Init((void*)0x400000, heapSize);



    // 4. Initialize TSS and Syscalls (Ring 3 support)
    TSS::Init((uint64_t)&kernel_tss_stack[4096]);
    GDT::LoadTSS(TSS::GetTSS());
    Syscall::Init();
    
    // 4b. Initialize Serial for debugging
    UART::Init();
    SerialDebug::SetOutput(OUTPUT_BOTH);
    UART::WriteLine("[Serial] COM1 @ 115200 baud initialized.");
    
    // 5. Initialize Graphics HAL
    Graphics::Init(&bootInfo->framebuffer);
    Verbose::Init();
    
    // 5a. Enable Write-Combining for framebuffer acceleration
    WriteCombining::InitPAT();
    
    Mouse::Init();
    Mouse::SetBounds(bootInfo->framebuffer.width, bootInfo->framebuffer.height);
    
    // 5b. Initialize Compositor
    Compositor::Init();


    // 6. Initialize HAL Device Registry
    DeviceRegistry::Init();

    // 7. Initialize VFS and InitRD
    VFS::Init();
    InitRD::Init();
    
    // 8. Load BootConfig from VFS
    BootConfiguration::Init();
    BootConfiguration::LoadFromFile("/etc/boot.cfg");
    
    // 8b. Initialize Keymap HAL with config
    KeymapHAL::Init();
    KeymapHAL::SetKeymap(BootConfiguration::GetKeymap());
    
    // 8c. Initialize Font Renderer
    FontRenderer::Init();
    
    // 8d. Initialize Audio HAL and Mixer
    Audio::Init();
    AudioMixer::Init();

    // 9. Initialize Drivers (register with HAL)
    PIT::Init(100); 
    Keyboard::Init();
    RAMDisk::Init();


    
    // 10. Enable Interrupts
    EarlyTerm::Print("[Kernel] Enabling Interrupts... ");
    IDT::EnableInterrupts();
    // Unmask: IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade to slave)
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xF8), "Nd"((uint16_t)0x21));
    // Unmask: IRQ12 (mouse) on slave = bit 4 of slave mask
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xEF), "Nd"((uint16_t)0xA1));
    EarlyTerm::Print("DONE. (IRQ0,1,2,12)\\n");
    
    // 7. Scheduler Init
    EarlyTerm::Print("[Kernel] Initializing Scheduler... ");
    Scheduler::Init();
    // BackgroundTask disabled - was showing Swift counter and interfering with graphics
    // Scheduler::CreateTask(BackgroundTask);
    EarlyTerm::Print("DONE.\n");


    EarlyTerm::Print("--------------------------------------------------\n");
    EarlyTerm::Print("Morphic OS - Phase Swift HAL (v0.5)\n");
    EarlyTerm::Print("Type 'help' for commands. HAL: Active.\n");
    
    Shell::Init();

    // Interactive Loop (Task 0)
    while(1) {
        char c = Keyboard::GetChar();
        if (c) {
            Shell::OnChar(c);
        }
    }
}
