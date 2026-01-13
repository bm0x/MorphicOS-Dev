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
#include "../arch/common/mmu.h"
#include "../mm/heap.h"
#include "../fs/vfs.h"
#include "../fs/initrd.h"
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

extern "C" void kernel_main(BootInfo* bootInfo) {
    // 0. Validate BootInfo
    if (!bootInfo || bootInfo->magic != 0xDEADBEEFCAFEBABE) {
        while(1) __asm__("hlt");
    }

    // 1. Initialize Display
    EarlyTerm::Init(&bootInfo->framebuffer);
    gFramebuffer = &bootInfo->framebuffer;
    
    // 2. Core Hardware Init
    HAL::Platform::Init();
    
    // PMM needs BootInfo, so it stays here for now (generic interface, specific implementation)
    PMM::Init(bootInfo);
    MMU::Init();  // Initialize MMU to capture UEFI page tables

    EarlyTerm::Print("[OK] Core Systems Validated.\n");

    // 3. Initialize Heap dynamically based on available RAM
    size_t freeRAM = PMM::GetFreeMemory();
    size_t heapSize = (freeRAM * 3) / 4;  // Use 75% of free RAM
    
    // Minimum 32MB, no maximum limit
    if (heapSize < 32 * 1024 * 1024) heapSize = 32 * 1024 * 1024;
    
    KHeap::Init((void*)0x400000, heapSize);


    
    // ===== TRACE CHECKPOINTS =====
    UART::Write("[BOOT-TRACE] After UART init\n");
    
    // 5. Initialize Graphics HAL
    Graphics::Init(&bootInfo->framebuffer);
    UART::Write("[BOOT-TRACE] After Graphics::Init\n");
    
    Verbose::Init();
    UART::Write("[BOOT-TRACE] After Verbose::Init\n");
    
    // 5a. Enable Write-Combining for framebuffer acceleration
    WriteCombining::InitPAT();
    UART::Write("[BOOT-TRACE] After WriteCombining::InitPAT\n");
    
    Mouse::Init();
    UART::Write("[BOOT-TRACE] After Mouse::Init\n");
    
    Mouse::SetBounds(bootInfo->framebuffer.width, bootInfo->framebuffer.height);
    UART::Write("[BOOT-TRACE] After Mouse::SetBounds\n");
    
    // 5b. Initialize Compositor
    UART::Write("[BOOT-TRACE] Skipping Compositor::Init to avoid large alloc crash\n");
    // Compositor::Init();
    UART::Write("[BOOT-TRACE] After Compositor::Init (Skipped)\n");

    // 6. Initialize HAL Device Registry
    DeviceRegistry::Init();
    UART::Write("[BOOT-TRACE] After DeviceRegistry::Init\n");

    // 7. Initialize VFS and InitRD
    VFS::Init();
    UART::Write("[BOOT-TRACE] After VFS::Init\n");
    
    InitRD::Init(bootInfo->initrdAddr, bootInfo->initrdSize);
    UART::Write("[BOOT-TRACE] After InitRD::Init\n");
    
    // 8. Load BootConfig from VFS
    BootConfiguration::Init();
    UART::Write("[BOOT-TRACE] After BootConfiguration::Init\n");
    
    BootConfiguration::LoadFromFile("/etc/boot.cfg");
    UART::Write("[BOOT-TRACE] After BootConfig::LoadFromFile\n");
    
    // 8b. Initialize Keymap HAL with config
    KeymapHAL::Init();
    UART::Write("[BOOT-TRACE] After KeymapHAL::Init\n");
    
    KeymapHAL::SetKeymap(BootConfiguration::GetKeymap());
    UART::Write("[BOOT-TRACE] After KeymapHAL::SetKeymap\n");
    
    // 8c. Initialize Font Renderer
    FontRenderer::Init();
    UART::Write("[BOOT-TRACE] After FontRenderer::Init\n");
    
    // 8d. Initialize Audio HAL and Mixer
    Audio::Init();
    UART::Write("[BOOT-TRACE] After Audio::Init\n");
    
    AudioMixer::Init();
    UART::Write("[BOOT-TRACE] After AudioMixer::Init\n");

    // 9. Initialize Drivers (register with HAL)
    // 1000Hz so PIT_GetTicks() ~= milliseconds.
    // Many parts of the system (SYS_SLEEP/SYS_GET_TIME_MS, GUI pacing) assume ms granularity.
    PIT::Init(1000);
    UART::Write("[BOOT-TRACE] After PIT::Init\n");
    
    // --- MOUSE DRIVER REGISTRATION ---
    // Moved to global scope to avoid local class issues
    DeviceRegistry::Register(DeviceType::INPUT, GetMouseAdapter());
    UART::Write("[BOOT-TRACE] Mouse Registered with InputManager\n");
    
    // Check if Keyboard namespace has OnInterrupt
    // Assuming Keyboard::Init and Keyboard::OnInterrupt exist (based on typical structure)
    // If NOT, we skip it or check header. 
    // Let's register Mouse first as priority.

    Keyboard::Init();
    UART::Write("[BOOT-TRACE] After Keyboard::Init\n");
    
    RAMDisk::Init();
    UART::Write("[BOOT-TRACE] After RAMDisk::Init\n");

    IDE::Init();
    UART::Write("[BOOT-TRACE] After IDE::Init\n");
    
    // 10. Enable Interrupts
    EarlyTerm::Print("[Kernel] Enabling Interrupts... ");
    UART::Write("[BOOT-TRACE] About to enable interrupts...\n");
    HAL::Platform::EnableInterrupts();
    UART::Write("[BOOT-TRACE] After IDT::EnableInterrupts\n");
    
    // Unmask: IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade to slave)
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xF8), "Nd"((uint16_t)0x21));
    // Unmask: IRQ12 (mouse) on slave = bit 4 of slave mask
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xEF), "Nd"((uint16_t)0xA1));
    EarlyTerm::Print("DONE. (IRQ0,1,2,12)\\n");
    UART::Write("[BOOT-TRACE] After IRQ unmasking\n");
    
    // 7. Scheduler Init
    EarlyTerm::Print("[Kernel] Initializing Scheduler... ");
    Scheduler::Init();
    UART::Write("[BOOT-TRACE] After Scheduler::Init\n");
    
    // BackgroundTask disabled - was showing Swift counter and interfering with graphics
    // Scheduler::CreateTask(BackgroundTask);
    EarlyTerm::Print("DONE.\n");


    EarlyTerm::Print("--------------------------------------------------\n");
    EarlyTerm::Print("Morphic OS - Phase Swift HAL (v0.5)\n");
    EarlyTerm::Print("Type 'help' for commands. HAL: Active.\n");
    UART::Write("[BOOT-TRACE] Kernel boot complete, entering AUTO-TEST\n");
    
    // =======================================================================
    // AUTO-TEST: Trigger package loader immediately for debugging
    // Comment out / remove this block after debugging is complete
    // =======================================================================
    UART::Write("\n");
    UART::Write("*****************************************************\n");
    UART::Write("*** AUTO-TEST: Loading desktop.mpk for debugging ***\n");
    UART::Write("*****************************************************\n");
    UART::Write("\n");
    
    EarlyTerm::Print("\n[AUTO-TEST] Loading desktop.mpk...\n");
    
    // Call loader directly (loader.h included at top)
    LoadedProcess proc = PackageLoader::Load("/initrd/desktop.mpk");
    
    if (proc.error_code == 0) {
        UART::Write("[AUTO-TEST] Load success. Entry: ");
        UART::WriteHex(proc.entry_point);
        UART::Write(" Stack: ");
        UART::WriteHex(proc.stack_top);
        UART::Write("\n");
        
        // Create User Task
        Scheduler::CreateUserTask((void(*)())proc.entry_point, (void*)proc.stack_top);
    } else {
        UART::Write("!!! PackageLoader::Load failed: ");
        UART::WriteDec(proc.error_code);
        UART::Write(" !!!\n");
    }
    
    EarlyTerm::Print("[AUTO-TEST] Load returned: ");
    EarlyTerm::PrintDec(proc.error_code);
    EarlyTerm::Print("\n");
    // =======================================================================
    // END AUTO-TEST
    // =======================================================================
    
    Shell::Init();

    // Interactive Loop (Task 0)
    while(1) {
        char c = Keyboard::GetChar();
        if (c) {
            Shell::OnChar(c);
        }
    }
}

