#include "../../shared/boot_info.h"
#include "../hal/video/early_term.h"
#include "../hal/video/graphics.h"
#include "../hal/video/verbose.h"
#include "../hal/video/compositor.h"
#include "../hal/video/font_renderer.h"
#include "../hal/device_registry.h"
#include "../hal/input/input_device.h"
#include "../hal/input/mouse.h"
#include "../hal/input/keymap.h"
#include "../hal/audio/audio_device.h"
#include "../hal/audio/mixer.h"
#include "../hal/serial/uart.h"
#include "../mm/pmm.h"
#include "../mm/write_combining.h"
#include "../hal/platform.h"
#include "../drivers/gpu/bga.h"
#include "../arch/common/mmu.h"
#include "../hal/arch/x86_64/pci.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../fs/initrd.h"
#include "../fs/drivers/fat32.h"
#include "../fs/mount_manager.h"
#include "shell.h"
#include "bootconfig.h"
#include "loader.h"
#include "../process/scheduler.h"
#include "../utils/std.h"


// Driver Headers
namespace PIT { void Init(uint32_t); }
namespace Keyboard { void Init(); char GetChar(); }
namespace RAMDisk { void Init(); }
namespace IDE { void Init(); }

// Kernel stack for TSS moved to HAL implementation

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

// Global Mouse Adapter
struct MouseAdapter : public IInputDevice {
    MouseAdapter() { 
        const char* s = "PS/2 Mouse";
        for(int i=0; i<31 && s[i]; i++) name[i] = s[i];
        name[31] = 0; // Null terminate
        
        init = Mouse::Init;
        on_interrupt = Mouse::OnInterrupt; 
        poll_event = nullptr; 
    }
};

IInputDevice* GetMouseAdapter() {
    static MouseAdapter instance;
    return &instance;
}

#include "../ui/bootscreen.h"

extern "C" void kernel_main(BootInfo* bootInfo) {
    // 0. Validate BootInfo
    if (!bootInfo || bootInfo->magic != 0xDEADBEEFCAFEBABE) {
        while(1) __asm__("hlt");
    }

    // 1. Initialize Display (Early Terminal for emergency logs)
    EarlyTerm::Init(&bootInfo->framebuffer);
    
    // ... Platform Init ...
    HAL::Platform::Init();
    
    PMM::Init(bootInfo);
    MMU::Init(); 

    EarlyTerm::Print("[OK] Core Systems Validated.\n");
    
    // Check ACPI/SMP Status
    if (bootInfo->rsdp) {
        EarlyTerm::Print("[ACPI] RSDP Pointer: ");
        EarlyTerm::PrintHex((uint64_t)bootInfo->rsdp);
        EarlyTerm::Print("\n");
        EarlyTerm::Print("[SMP] Hardware discovery enabled.\n");
    } else {
        EarlyTerm::Print("[ACPI] WARNING: RSDP not found. SMP disabled.\n");
    }
    
    // CRITICAL: Initialize Graphics HAL *BEFORE* Heap
    Graphics::Init(&bootInfo->framebuffer);
    
    // Initialize FontRenderer EARLY for BootScreen
    FontRenderer::Init();
    
    // === START BOOT ANIMATION ===
    BootScreen::Init();
    BootScreen::Update(10, "Initializing Memory Manager...");
    
    // 3. Initialize Heap (Bestial Configuration)
    size_t freeRAM = PMM::GetFreeMemory();
    size_t heapSize = (freeRAM * 3) / 4;  
    // Boost minimum heap to 128MB for extreme stability
    if (heapSize < 128 * 1024 * 1024) heapSize = 128 * 1024 * 1024;
    
    void* heapBase = PMM::AllocContiguous(heapSize / 4096);
    if (!heapBase) {
        // Retry logic with safe fallback (256MB)
         heapSize = 256 * 1024 * 1024;
         heapBase = PMM::AllocContiguous(heapSize / 4096);
    }
    
    if (!heapBase) {
        EarlyTerm::Print("[KERNEL] CRITICAL: Failed to allocate KHeap!\n");
        while(1) __asm__("hlt");
    }
    
    KHeap::Init(heapBase, heapSize); 
    BootScreen::Update(20, "Memory Initialized");

    // ===== TRACE CHECKPOINTS =====
    UART::Write("[BOOT-TRACE] After UART init\n"); 
    
    Verbose::Init();
    UART::Write("[BOOT-TRACE] After Verbose::Init\n");
    
    // Initialize PCI Subsystem
    HAL::PCI::Init();
    UART::Write("[BOOT-TRACE] After PCI::Init\n");
    
    // NOTE: BGA is already initialized in Graphics::Init() above
    // Do NOT create another BGADriver instance here - it would corrupt the hardware state
    UART::Write("[BOOT-TRACE] BGA handled by Graphics::Init\n");
    
    // 5a. Enable Write-Combining for framebuffer acceleration
    WriteCombining::InitPAT();
    UART::Write("[BOOT-TRACE] After WriteCombining::InitPAT\n");
    
    Mouse::Init();
    Mouse::SetBounds(bootInfo->framebuffer.width, bootInfo->framebuffer.height);
    UART::Write("[BOOT-TRACE] After Mouse::Init\n");
    
    BootScreen::Update(30, "Input Devices Ready");

    // 6. Initialize HAL Device Registry
    DeviceRegistry::Init();
    
    BootScreen::Update(40, "Loading Virtual File System...");
    VFS::Init();
    FAT32::Init(); // Register FAT32 driver
    InitRD::Init(bootInfo->initrdAddr, bootInfo->initrdSize);
    
    BootScreen::Update(50, "Reading Boot Configuration...");
    BootConfiguration::Init();
    BootConfiguration::LoadFromFile("/etc/boot.cfg");
    KeymapHAL::Init();
    KeymapHAL::SetKeymap(BootConfiguration::GetKeymap());
    
    // Audio
    BootScreen::Update(60, "Initializing Audio Subsystem...");
    Audio::Init();
    AudioMixer::Init();

    // Drivers
    BootScreen::Update(70, "Starting Hardware Drivers...");
    PIT::Init(1000);
    UART::Write("[BOOT-TRACE] After PIT::Init\n");
    
    DeviceRegistry::Register(DeviceType::INPUT, GetMouseAdapter());
    UART::Write("[BOOT-TRACE] Mouse Registered\n");
    
    Keyboard::Init();
    UART::Write("[BOOT-TRACE] After Keyboard::Init\n");
    
    BootScreen::Update(80, "Mounting Disks...");
    RAMDisk::Init();
    IDE::Init();
    UART::Write("[BOOT-TRACE] After Storage Init\n");
    
    // Auto-mount detected filesystems (FAT32 on debug_disk, etc.)
    MountManager::Init();
    MountManager::ScanAndMount();
    UART::Write("[BOOT-TRACE] After MountManager ScanAndMount\n");
    
    // Interrupts
    UART::Write("[BOOT-TRACE] About to call BootScreen::Update(90)...\n");
    BootScreen::Update(90, "Enabling Interrupts...");
    UART::Write("[BOOT-TRACE] About to EnableInterrupts...\n");
    HAL::Platform::EnableInterrupts();
    UART::Write("[BOOT-TRACE] About to unmask IRQs...\n");
    // Unmask IRQs...
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xF8), "Nd"((uint16_t)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xEF), "Nd"((uint16_t)0xA1));
    UART::Write("[BOOT-TRACE] IRQs unmasked, about to init Scheduler...\n");
    
    // Scheduler
    BootScreen::Update(95, "Starting Scheduler...");
    UART::Write("[BOOT-TRACE] About to call Scheduler::Init()...\n");
    Scheduler::Init();
    UART::Write("[BOOT-TRACE] Scheduler::Init() complete!\n");

    UART::Write("[BOOT-TRACE] About to call BootScreen::Update(100)...\n");
    BootScreen::Update(100, "System Ready. Launching Desktop...");
    UART::Write("[BOOT-TRACE] BootScreen::Update(100) done, entering delay loop...\n");
    // Artificial delay to see the 100% (optional)
    for(volatile int i=0; i<5000000; i++); 
    UART::Write("[BOOT-TRACE] Delay done, calling BootScreen::Finish()...\n");
    
    BootScreen::Finish(); 
    UART::Write("[BOOT-TRACE] BootScreen::Finish() done\n");

    EarlyTerm::Print("--------------------------------------------------\n");
    EarlyTerm::Print("Morphic OS Kernel (v0.6)\n");
    EarlyTerm::Print("Type 'help' for commands. HAL: Active.\n");
    UART::Write("[BOOT-TRACE] Kernel boot complete, entering AUTO-TEST\n");
    
    // =======================================================================
    // AUTO-TEST: Trigger package loader immediately for debugging
    // Comment out / remove this block after debugging is complete
    // =======================================================================
    UART::Write("\n");
    UART::Write("*****************************************************\n");
    UART::Write("***            Desktop.mpk: GUI Desktop            ***\n");
    UART::Write("*****************************************************\n");
    UART::Write("\n");
    EarlyTerm::Print("\n[Desktop.mpk] Loading...\n");
    UART::Write("\n");
    
    // Call loader directly (loader.h included at top)
    LoadedProcess proc = PackageLoader::Load("/initrd/desktop.mpk");
    
    if (proc.error_code == 0) {
        UART::Write("[Desktop.mpk] Load success. Entry: ");
        UART::WriteHex(proc.entry_point);
        UART::Write(" Stack: ");
        UART::WriteHex(proc.stack_top);
        UART::Write("\n");
        
        // Create User Task
        // For the Desktop (AUTO-TEST), we use the current kernel CR3 (shared space)
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        Scheduler::CreateUserTask((void(*)())proc.entry_point, (void*)proc.stack_top, cr3, proc.arg1);
    } else {
        UART::Write("!!! Desktop.mpk: PackageLoader::Load failed: ");
        UART::WriteDec(proc.error_code);
        UART::Write(" !!!\n");
    }
    
    EarlyTerm::Print("[Desktop.mpk] Load returned: ");
    EarlyTerm::PrintDec(proc.error_code);
    EarlyTerm::Print("\n");
    // =======================================================================
    // END AUTO-TEST
    // =======================================================================
    
    // NOTE: Shell is disabled when Desktop runs.
    // The Desktop handles all user interaction via sys_get_event().
    // Kernel Task 0 becomes an idle task.
    // Shell::Init();  // DISABLED - conflicts with Desktop graphics
    
    // Idle Loop (Task 0) - Just wait for interrupts
    // The Scheduler will preempt us to run Desktop and other tasks
    while(1) {
        __asm__ volatile("hlt");  // Wait for interrupt (low power)
    }
}

