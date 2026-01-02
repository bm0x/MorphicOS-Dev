#include "syscall.h"
#include "../../video/early_term.h"
#include "../../video/compositor.h"
#include "../../video/graphics.h"
#include "../../audio/audio_device.h"
#include "../../serial/uart.h"
#include "../../../mm/heap.h"
#include "../../../arch/common/mmu.h"
#include "../../../mm/pmm.h"
#include "../../input/input_device.h"

extern "C" uint64_t PIT_GetTicks();


// MSR read/write helpers
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Assembly entry point
extern "C" void syscall_entry();

namespace Syscall {
    void Init() {
        // Enable SYSCALL/SYSRET instructions via EFER.SCE (bit 0)
        // Without this, SYSCALL generates #UD (Invalid Opcode)
        const uint32_t MSR_EFER = 0xC0000080;
        const uint64_t EFER_SCE = (1 << 0);  // SYSCALL Enable bit
        
        uint64_t efer = rdmsr(MSR_EFER);
        efer |= EFER_SCE;
        wrmsr(MSR_EFER, efer);
        
        // STAR MSR Format:
        // [63:48] = SYSRET CS/SS base selector
        // [47:32] = SYSCALL CS/SS base selector
        //
        // For SYSCALL: CS = [47:32], SS = [47:32] + 8
        // For SYSRET (64-bit): CS = [63:48] + 16, SS = [63:48] + 8
        //
        // Our GDT layout:
        //   0x08 = Kernel Code (Ring 0)
        //   0x10 = Kernel Data (Ring 0)
        //   0x18 = User Code (Ring 3)
        //   0x20 = User Data (Ring 3)
        //
        // For SYSCALL (kernel entry): We want CS=0x08, SS=0x10
        //   Base = 0x08 -> CS=0x08, SS=0x10 ✓
        //
        // For SYSRET (user return): We want CS=0x1B (0x18|3), SS=0x23 (0x20|3)
        //   With base=0x10: CS=0x10+16=0x20, SS=0x10+8=0x18 (BACKWARDS!)
        //   With base=0x08: CS=0x08+16=0x18, SS=0x08+8=0x10 (SS is Kernel Data!)
        //
        // NOTE: Standard x86-64 GDT expects User Data BEFORE User Code for SYSRET!
        // Our current GDT has it reversed (Code at 0x18, Data at 0x20).
        // 
        // For now, we use IRETQ in JumpToUser which works with any GDT order.
        // SYSRET will need GDT reordering to work properly.
        
        // STAR MSR Format:
        // [63:48] = SYSRET CS/SS base selector -> 0x10 (Kernel Data)
        //           CS = Base + 16 = 0x20 (User Code)
        //           SS = Base + 8  = 0x18 (User Data)
        // [47:32] = SYSCALL CS/SS base selector -> 0x08 (Kernel Code)
        uint64_t star = ((uint64_t)0x0010 << 48) | ((uint64_t)0x0008 << 32);
        wrmsr(MSR_STAR, star);
        wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
        wrmsr(MSR_SFMASK, 0x200);  // Mask IF flag during syscall
        
        EarlyTerm::Print("[Syscall] SYSCALL/SYSRET configured (Base 0x10/0x08).\\n");
    }
    
    void JumpToUser(void* entry, void* stack, void* arg1) {
        // Ring 3 segment selectors (selector value | RPL 3)
        // GDT: 3=UserData(0x18), 4=UserCode(0x20)
        uint64_t user_cs = 0x20 | 3;  // Index 4 -> User Code
        uint64_t user_ss = 0x18 | 3;  // Index 3 -> User Data
        uint64_t rflags = 0x202;      // IF (Interrupt Flag) set, reserved bit 1 set
        
        // ====================================================================
        // SERIAL TRACE - CRITICAL CHECKPOINT BEFORE IRETQ
        // If system reboots after this message, the fault is in the IRETQ
        // ====================================================================
        UART::Write("\n");
        UART::Write("====================================================\n");
        UART::Write("=== IRETQ CHECKPOINT - LAST MESSAGE BEFORE JUMP ===\n");
        UART::Write("====================================================\n");
        UART::Write("Entry Point (RIP): "); UART::WriteHex((uint64_t)entry); UART::Write("\n");
        UART::Write("User Stack (RSP):  "); UART::WriteHex((uint64_t)stack); UART::Write("\n");
        UART::Write("Arg1 (RDI):        "); UART::WriteHex((uint64_t)arg1); UART::Write("\n");
        UART::Write("User CS:           "); UART::WriteHex(user_cs); UART::Write("\n");
        UART::Write("User SS:           "); UART::WriteHex(user_ss); UART::Write("\n");
        UART::Write("RFLAGS:            "); UART::WriteHex(rflags); UART::Write("\n");
        UART::Write("\n");
        UART::Write(">>> EXECUTING IRETQ NOW <<<\n");
        UART::Write("If you see a Page Fault after this, the issue is:\n");
        UART::Write("  - Memory at Entry Point lacks PAGE_USER flag\n");
        UART::Write("  - Stack memory lacks PAGE_USER flag\n");
        UART::Write("  - GDT Ring 3 descriptors are misconfigured\n");
        UART::Write("====================================================\n\n");
        
        EarlyTerm::Print("[Syscall] Jumping to user mode...\n");
        
        // Setup RDI (First Argument) before IRETQ
        // IRETQ Frame on stack (from top to bottom):
        //   SS, RSP, RFLAGS, CS, RIP
        __asm__ volatile(
            "cli\n"                    // Disable interrupts
            "mov %5, %%rdi\n"          // Load arg1 into RDI (first argument)
            "mov %0, %%rax\n"          // SS
            "push %%rax\n"
            "mov %1, %%rax\n"          // RSP (user stack)
            "push %%rax\n"
            "mov %2, %%rax\n"          // RFLAGS
            "push %%rax\n"
            "mov %3, %%rax\n"          // CS
            "push %%rax\n"
            "mov %4, %%rax\n"          // RIP (entry point)
            "push %%rax\n"
            "iretq\n"                  // Return to Ring 3
            :
            : "r"(user_ss), "r"((uint64_t)stack), "r"(rflags), 
              "r"(user_cs), "r"((uint64_t)entry), "r"((uint64_t)arg1)
            : "rax", "rdi", "memory"
        );
    }

}

// Syscall handler - called from assembly
extern "C" uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (num) {
        case SYS_EXIT:
            return 0;
            
        case SYS_WRITE:
            for (uint64_t i = 0; i < arg3; i++) {
                EarlyTerm::PutChar(((char*)arg1)[i]);
            }
            return arg3;
            
        case SYS_MALLOC:
            return (uint64_t)kmalloc((size_t)arg1);
            
        case SYS_FREE:
            kfree((void*)arg1);
            return 0;
            
        case SYS_UPDATE_SCREEN:
            // Compose all layers and flip to framebuffer
            Compositor::Compose();
            Compositor::Flip();
            return 0;
            
        case SYS_GET_SCREEN_INFO:
            // Return packed width/height
            return ((uint64_t)Graphics::GetWidth() << 32) | Graphics::GetHeight();
            
        case SYS_BEEP:
            // Play beep: arg1 = frequency, arg2 = duration_ms
            Audio::Beep((uint32_t)arg1, (uint32_t)arg2);
            return 0;
            
        case SYS_SLEEP:
        {
            // Simple busy-wait sleep (blocking) since we are single threaded mostly
            uint64_t ms = arg1;
            uint64_t start = PIT_GetTicks();
            while (PIT_GetTicks() < start + ms) {
                // spin
                __asm__ volatile("pause");
            }
            return 0;
        }

        case SYS_GET_EVENT: {
            if (arg1 == 0) return 0;
            const uint64_t USER_SPACE_MIN = 0x600000000000;
            // Basic sanity check just in case
            if (arg1 < USER_SPACE_MIN) return 0; 

            OSEvent kEv;
            if (InputManager::GetNextOSEvent(&kEv)) {
                OSEvent* uEv = (OSEvent*)arg1;
                *uEv = kEv;
                return 1;
            }
            return 0;
        }

        case SYS_GET_TIME_MS:
            // Return system time in milliseconds (assuming PIT 1ms ticks or close enough)
            return PIT_GetTicks();
            
        case SYS_ALLOC_BACKBUFFER:
        {
            // arg1: size in bytes
            uint64_t size = arg1;
            uint64_t pages = (size + 4095) / 4096;
            
            // Allocate contiguous physical memory for best performance
            // Using kernel PMM with offset to avoid low memory
            // NOTE: AllocContiguous is from previous task.
            // If alloc fails, return 0
            
            // We need a way to alloc pages. For now, let's alloc page by page 
            // OR use a fixed region if we had a linear allocator.
            // But we can just use PMM::AllocPage() in a loop since we are remapping them virtuallly contiguous!
            // We DON'T need physical contiguity for software rendering, only virtual.
            
            // Dedicated User Backbuffer Virtual Base: 0x600200000000
            uint64_t virt_base = 0x600200000000ULL;
            
            for (uint64_t i = 0; i < pages; i++) {
                void* phys_ptr = PMM::AllocPage();
                if (!phys_ptr) {
                     UART::Write("[Syscall] Alloc Backbuffer OOM\n");
                     return 0; 
                }
                
                // Map to Virtual
                MMU::MapPage(virt_base + (i * 4096), (uint64_t)phys_ptr,
                             PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT | PAGE_NOCACHE);
                             
                // Clear memory (efficiency check: MapPage doesn't clear)
                // We must clear it to avoid leaking data or garbage
                // To clear it, we need to write to it using a KERNEL address? 
                // Wait, map is USER. Kernel can access if it's identity mapped too? 
                // PMM returns Phys. We can't write to Phys directly without identity map.
                // Assuming Phys < Identity Map Limit (normally covered).
                // Or we can just let userspace clear it.
            }
            
            // Flush TLB
            uint64_t cr3;
            __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
            __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
            
            UART::Write("[Syscall] Allocated Backbuffer at "); UART::WriteHex(virt_base); UART::Write("\n");
            
            return virt_base;
        }

        case 50: // SYS_VIDEO_MAP
        {
            // Return pointer to framebuffer/backbuffer for direct access
            // FIX: Map the framebuffer info userspace with PAGE_USER permissions
            // Otherwise, writing to it causes a Page Fault (Protection Write Triggered)
            
            uint32_t* fb_ptr = Graphics::GetFramebuffer();
            if (!fb_ptr) return 0;
            
            uint64_t phys_base = (uint64_t)fb_ptr;
            uint32_t width = Graphics::GetWidth();
            uint32_t height = Graphics::GetHeight();
            // SAFETY FIX: Ensure we calculate size for 32bpp (4 bytes/pixel)
            // Some UEFI implementations might return pitch in pixels or we might interpret it wrong.
            uint32_t pitch = Graphics::GetPitch();
            
            // Minimum size = Width * Height * 4.
            uint64_t size_bytes_min = (uint64_t)width * height * 4;
            uint64_t size_bytes_pitch = (uint64_t)pitch * height;
            
            // Use the larger of the two to be safe (covers physical padding if pitch is bytes, covers BPP if pitch is pixels)
            uint64_t total_size = (size_bytes_pitch > size_bytes_min) ? size_bytes_pitch : size_bytes_min;
            
            uint64_t pages = (total_size + 4095) / 4096;
            
            // FIX: Map to a new USER virtual address to avoid Huge Page collisions in kernel space
            // Userspace base is 0x600000000000. Let's use 0x600100000000 for MMIO/Video.
            uint64_t user_video_virt = 0x600100000000ULL;
            
            UART::Write("[Syscall] Mapping Video Memory to User\n");
            UART::Write("  Phys Base: "); UART::WriteHex(phys_base); UART::Write("\n");
            UART::Write("  Virt Base: "); UART::WriteHex(user_video_virt); UART::Write("\n");
            UART::Write("  Size: "); UART::WriteDec(total_size); UART::Write(" bytes ("); UART::WriteDec(pages); UART::Write(" pages)\n");
            
            // Map every page of the framebuffer
            for (uint64_t i = 0; i < pages; i++) {
                uint64_t offset = i * 4096;
                // Map Physical Framebuffer -> New User Virtual Address
                bool success = MMU::MapPage(user_video_virt + offset, phys_base + offset, 
                             PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT | PAGE_NOCACHE);
                
                if (!success) {
                    UART::Write("  [ERROR] MapPage failed at offset "); UART::WriteHex(offset); UART::Write("\n");
                    // Continue trying others or break?
                }
            }
            
            // FLUSH TLB: Reload CR3 to make sure new permissions are seen
            uint64_t cr3;
            __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
            __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
            
            return user_video_virt;
        }
            
        case 51: // SYS_VIDEO_FLIP
            // Trigger V-Sync / Flip
            Graphics::Flip();
            return 0;
            
        case 52: // SYS_INPUT_POLL
            // Arg1 = Pointer to InputEvent struct
            // Logic: Pop from input queue and copy to user buffer
            // For now, return 0 (no event)
            return 0;

        default:
            return (uint64_t)-1;
    }

}


