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
#include "../x86_64/io.h"
#include "../../../mm/pmm.h"
#include "../../storage/block_device.h"
#include "system_info.h"
#include "../../input/keymap.h"
#include "../../../core/loader.h"
#include "../../../process/scheduler.h"
#include "../../platform.h"

extern "C" uint64_t PIT_GetTicks();

// SIMD optimized memory operations (from blit_fast.S)
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
}

static bool WaitVBlankTimeoutMs(uint32_t timeout_ms)
{
    const uint16_t VGA_STATUS_PORT = 0x3DA;
    const uint8_t VSYNC_BIT = 0x08;

    uint64_t deadline = PIT_GetTicks() + (uint64_t)timeout_ms;

    // Wait for end of current retrace.
    while ((IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) != 0)
    {
        if (PIT_GetTicks() >= deadline) return false;
        __asm__ volatile("pause");
    }

    // Wait for start of next retrace.
    while ((IO::inb(VGA_STATUS_PORT) & VSYNC_BIT) == 0)
    {
        if (PIT_GetTicks() >= deadline) return false;
        __asm__ volatile("pause");
    }

    return true;
}

static inline void Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(subleaf));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static void GetCpuVendor(char out[13])
{
    uint32_t eax, ebx, ecx, edx;
    Cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    ((uint32_t*)out)[0] = ebx;
    ((uint32_t*)out)[1] = edx;
    ((uint32_t*)out)[2] = ecx;
    out[12] = '\0';
}

static void GetCpuBrand(char out[49])
{
    uint32_t maxEax, ebx, ecx, edx;
    Cpuid(0x80000000, 0, &maxEax, &ebx, &ecx, &edx);
    if (maxEax < 0x80000004)
    {
        out[0] = '\0';
        return;
    }

    uint32_t* p = (uint32_t*)out;
    Cpuid(0x80000002, 0, &p[0], &p[1], &p[2], &p[3]);
    Cpuid(0x80000003, 0, &p[4], &p[5], &p[6], &p[7]);
    Cpuid(0x80000004, 0, &p[8], &p[9], &p[10], &p[11]);
    out[48] = '\0';
}

static inline uint8_t CmosRead(uint8_t reg)
{
    // Disable NMI while selecting register.
    IO::outb(0x70, (uint8_t)(reg | 0x80));
    return IO::inb(0x71);
}

static inline bool RtcUpdateInProgress()
{
    return (CmosRead(0x0A) & 0x80) != 0;
}

static inline uint8_t FromBcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static bool ReadRtcStable(MorphicDateTime* out)
{
    if (!out) return false;

    // Wait for UIP to clear (bounded).
    for (int i = 0; i < 100000; i++)
    {
        if (!RtcUpdateInProgress()) break;
        __asm__ volatile("pause");
    }

    uint8_t sec1, min1, hour1, day1, mon1, year1, cent1;
    uint8_t sec2, min2, hour2, day2, mon2, year2, cent2;
    uint8_t regB1, regB2;

    // Read twice until consistent.
    for (int attempt = 0; attempt < 8; attempt++)
    {
        while (RtcUpdateInProgress()) { __asm__ volatile("pause"); }
        sec1 = CmosRead(0x00);
        min1 = CmosRead(0x02);
        hour1 = CmosRead(0x04);
        day1 = CmosRead(0x07);
        mon1 = CmosRead(0x08);
        year1 = CmosRead(0x09);
        cent1 = CmosRead(0x32);
        regB1 = CmosRead(0x0B);

        while (RtcUpdateInProgress()) { __asm__ volatile("pause"); }
        sec2 = CmosRead(0x00);
        min2 = CmosRead(0x02);
        hour2 = CmosRead(0x04);
        day2 = CmosRead(0x07);
        mon2 = CmosRead(0x08);
        year2 = CmosRead(0x09);
        cent2 = CmosRead(0x32);
        regB2 = CmosRead(0x0B);

        if (sec1 == sec2 && min1 == min2 && hour1 == hour2 && day1 == day2 && mon1 == mon2 && year1 == year2 && cent1 == cent2 && regB1 == regB2)
            break;
    }

    uint8_t regB = regB2;
    bool isBinary = (regB & 0x04) != 0;
    bool is24h = (regB & 0x02) != 0;

    uint8_t sec = isBinary ? sec2 : FromBcd(sec2);
    uint8_t min = isBinary ? min2 : FromBcd(min2);

    uint8_t hourRaw = hour2;
    uint8_t hour;
    if (!is24h)
    {
        // 12h format: bit 7 is PM flag.
        bool pm = (hourRaw & 0x80) != 0;
        hourRaw &= 0x7F;
        hour = isBinary ? hourRaw : FromBcd(hourRaw);
        if (hour == 12) hour = 0;
        if (pm) hour = (uint8_t)(hour + 12);
    }
    else
    {
        hour = isBinary ? hourRaw : FromBcd(hourRaw);
    }

    uint8_t day = isBinary ? day2 : FromBcd(day2);
    uint8_t mon = isBinary ? mon2 : FromBcd(mon2);
    uint8_t year = isBinary ? year2 : FromBcd(year2);
    uint8_t cent = isBinary ? cent2 : FromBcd(cent2);

    uint16_t fullYear = 0;
    if (cent != 0)
        fullYear = (uint16_t)(cent * 100 + year);
    else
        fullYear = (uint16_t)(2000 + year);

    out->year = fullYear;
    out->month = mon;
    out->day = day;
    out->hour = hour;
    out->minute = min;
    out->second = sec;
    out->valid = 1;
    out->reserved0 = 0;
    return true;
}

static inline uint32_t ClampU32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#ifdef MOUSE_DEBUG
static uint64_t g_sysGetEventDelivered = 0;
static uint64_t g_sysGetEventCalls = 0;
#endif

// MSR read/write helpers
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Assembly entry point
extern "C" void syscall_entry();

namespace Syscall
{
    void Init()
    {
        // Enable SYSCALL/SYSRET instructions via EFER.SCE (bit 0)
        // Without this, SYSCALL generates #UD (Invalid Opcode)
        const uint32_t MSR_EFER = 0xC0000080;
        const uint64_t EFER_SCE = (1 << 0); // SYSCALL Enable bit

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
        // IMPORTANT:
        // MASK IF during syscall to prevent timer interrupts from causing context switch
        // while kernel stack is in use. This prevents stack corruption when scheduler
        // updates TSS.RSP0 mid-syscall.
        // Syscalls that need interrupts (like SYS_SLEEP) must manually enable them.
        wrmsr(MSR_SFMASK, 0x200); // Mask IF (bit 9)

        EarlyTerm::Print("[Syscall] SYSCALL/SYSRET configured (Base 0x10/0x08).\\n");
    }

    void JumpToUser(void *entry, void *stack, void *arg1)
    {
        // Ring 3 segment selectors (selector value | RPL 3)
        // GDT: 3=UserData(0x18), 4=UserCode(0x20)
        uint64_t user_cs = 0x20 | 3; // Index 4 -> User Code
        uint64_t user_ss = 0x18 | 3; // Index 3 -> User Data
        uint64_t rflags = 0x202;     // IF (Interrupt Flag) set, reserved bit 1 set

        // ====================================================================
        // SERIAL TRACE - CRITICAL CHECKPOINT BEFORE IRETQ
        // If system reboots after this message, the fault is in the IRETQ
        // ====================================================================
        UART::Write("\n");
        UART::Write("====================================================\n");
        UART::Write("=== IRETQ CHECKPOINT - LAST MESSAGE BEFORE JUMP ===\n");
        UART::Write("====================================================\n");
        UART::Write("Entry Point (RIP): ");
        UART::WriteHex((uint64_t)entry);
        UART::Write("\n");
        UART::Write("User Stack (RSP):  ");
        UART::WriteHex((uint64_t)stack);
        UART::Write("\n");
        UART::Write("Arg1 (RDI):        ");
        UART::WriteHex((uint64_t)arg1);
        UART::Write("\n");
        UART::Write("User CS:           ");
        UART::WriteHex(user_cs);
        UART::Write("\n");
        UART::Write("User SS:           ");
        UART::WriteHex(user_ss);
        UART::Write("\n");
        UART::Write("RFLAGS:            ");
        UART::WriteHex(rflags);
        UART::Write("\n");
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
            "cli\n"           // Disable interrupts
            "mov %5, %%rdi\n" // Load arg1 into RDI (first argument)
            "mov %0, %%rax\n" // SS
            "push %%rax\n"
            "mov %1, %%rax\n" // RSP (user stack)
            "push %%rax\n"
            "mov %2, %%rax\n" // RFLAGS
            "push %%rax\n"
            "mov %3, %%rax\n" // CS
            "push %%rax\n"
            "mov %4, %%rax\n" // RIP (entry point)
            "push %%rax\n"
            "iretq\n" // Return to Ring 3
            :
            : "r"(user_ss), "r"((uint64_t)stack), "r"(rflags),
              "r"(user_cs), "r"((uint64_t)entry), "r"((uint64_t)arg1)
            : "rax", "rdi", "memory");
    }

}

// Syscall handler - called from assembly
extern "C" uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    // Trace syscalls from Calculator (task 2+) to identify crash
    static uint64_t callCount = 0;
    uint64_t taskId = Scheduler::GetCurrentTaskId();
    if (taskId >= 2 && callCount < 50) {
        UART::Write("[Syscall] task=");
        UART::WriteDec(taskId);
        UART::Write(" num=");
        UART::WriteDec(num);
        UART::Write("\n");
        callCount++;
    }
    
    switch (num)
    {
    case SYS_EXIT:
        return 0;

    case SYS_WRITE:
        for (uint64_t i = 0; i < arg3; i++)
        {
            EarlyTerm::PutChar(((char *)arg1)[i]);
        }
        return arg3;

    case SYS_MALLOC:
        return (uint64_t)kmalloc((size_t)arg1);

    case SYS_FREE:
        kfree((void *)arg1);
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
        // Non-blocking sleep using scheduler
        Scheduler::Sleep(arg1);
        return 0;
    }

    case SYS_GET_EVENT:
    {
#ifdef MOUSE_DEBUG
        g_sysGetEventCalls++;
        // Print early and periodically even if no events are delivered.
        if (g_sysGetEventCalls <= 5 || ((g_sysGetEventCalls & 0xFFF) == 0))
        {
            UART::Write("[Syscall] GET_EVENT call #");
            UART::WriteDec((int64_t)g_sysGetEventCalls);
            UART::Write(" delivered=");
            UART::WriteDec((int64_t)g_sysGetEventDelivered);
            UART::Write("\n");
        }
#endif
        if (arg1 == 0)
            return 0;
        const uint64_t USER_SPACE_MIN = 0x600000000000;
        // Basic sanity check just in case
        if (arg1 < USER_SPACE_MIN)
            return 0;

        OSEvent kEv;
        if (InputManager::GetNextOSEvent(&kEv))
        {
            OSEvent *uEv = (OSEvent *)arg1;
            *uEv = kEv;

#ifdef MOUSE_DEBUG
            g_sysGetEventDelivered++;
            // Print the first few deliveries, then ~every 256 to avoid spam.
            if (g_sysGetEventDelivered <= 5 || ((g_sysGetEventDelivered & 0xFF) == 0))
            {
                UART::Write("[Syscall] GET_EVENT #");
                UART::WriteDec((int64_t)g_sysGetEventDelivered);
                UART::Write(" type=");
                UART::WriteDec((int64_t)kEv.type);
                UART::Write(" dx=");
                UART::WriteDec((int64_t)kEv.dx);
                UART::Write(" dy=");
                UART::WriteDec((int64_t)kEv.dy);
                UART::Write(" btn=");
                UART::WriteHex(kEv.buttons);
                UART::Write("\n");
            }
#endif
            return 1;
        }
        return 0;
    }

    case SYS_GET_TIME_MS:
        // Return system time in milliseconds (assuming PIT 1ms ticks or close enough)
        return PIT_GetTicks();

    case 55: // SYS_GET_RTC_DATETIME
    {
        if (arg1 == 0)
            return 0;
        const uint64_t USER_SPACE_MIN = 0x600000000000ULL;
        if (arg1 < USER_SPACE_MIN)
            return 0;

        MorphicDateTime dt = {};
        bool ok = ReadRtcStable(&dt);
        MorphicDateTime* u = (MorphicDateTime*)arg1;
        *u = dt;
        return ok ? 1 : 0;
    }

    case 56: // SYS_GET_SYSTEM_INFO
    {
        if (arg1 == 0)
            return 0;
        const uint64_t USER_SPACE_MIN = 0x600000000000ULL;
        if (arg1 < USER_SPACE_MIN)
            return 0;

        MorphicSystemInfo si = {};
        GetCpuVendor(si.cpu_vendor);
        GetCpuBrand(si.cpu_brand);

        si.total_mem_bytes = (uint64_t)PMM::GetTotalMemory();
        si.free_mem_bytes = (uint64_t)PMM::GetFreeMemory();

        si.fb_width = Graphics::GetWidth();
        si.fb_height = Graphics::GetHeight();
        si.fb_pitch = Graphics::GetPitch();
        si.fb_bpp = 32;

        // Get Disk Info (Find largest device)
        uint64_t max_size = 0;
        uint32_t dev_count = StorageManager::GetDeviceCount();
        for (uint32_t i = 0; i < dev_count; i++) {
            IBlockDevice* dev = StorageManager::GetDeviceByIndex(i);
            if (dev && dev->geometry.total_bytes > max_size) {
                max_size = dev->geometry.total_bytes;
            }
        }
        si.disk_total_bytes = max_size;
        si.disk_free_bytes = 0; // TODO: Implement FS free space check

        MorphicSystemInfo* u = (MorphicSystemInfo*)arg1;
        *u = si;
        return 1;
    }

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

        for (uint64_t i = 0; i < pages; i++)
        {
            void *phys_ptr = PMM::AllocPage();
            if (!phys_ptr)
            {
                UART::Write("[Syscall] Alloc Backbuffer OOM\n");
                return 0;
            }

            // Map to Virtual
            MMU::MapPage(virt_base + (i * 4096), (uint64_t)phys_ptr,
                         // IMPORTANT: This is regular RAM used for software rendering.
                         // It must be cacheable or performance will be terrible (stutter/lag).
                         PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);

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
        __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");

        UART::Write("[Syscall] Allocated Backbuffer at ");
        UART::WriteHex(virt_base);
        UART::Write("\n");

        return virt_base;
    }

    case 50: // SYS_VIDEO_MAP
    {
        UART::Write("[SYS_VIDEO_MAP] Entry - caller task: ");
        UART::WriteDec(Scheduler::GetCurrentTaskId());
        UART::Write("\n");
        
        // For Desktop (compositor), we need to share the SAME backbuffer as the kernel.
        // This ensures Desktop's drawing and ComposeAppWindowsOnly() operate on the same memory.
        //
        // We map the kernel's Graphics::GetBackbuffer() directly to userspace.
        // This avoids the "two buffer" problem where Desktop draws to one buffer
        // and kernel overlay draws to another.
        
        uint32_t width = Graphics::GetWidth();
        uint32_t height = Graphics::GetHeight();
        
        // Get kernel's backbuffer physical address
        uint32_t* kernelBackbuf = Graphics::GetBackbuffer();
        if (!kernelBackbuf) {
            UART::Write("[Syscall] SYS_VIDEO_MAP: No kernel backbuffer!\n");
            return 0;
        }
        
        // The kernel backbuffer is in identity-mapped kernel space
        // We need to map it to userspace
        uint64_t user_video_virt = 0x600100000000ULL;
        uint64_t size = (uint64_t)width * height * 4;
        uint64_t pages = (size + 4095) / 4096;
        
        // Map each page of kernel backbuffer to userspace
        for (uint64_t i = 0; i < pages; i++) {
            uint64_t phys_addr = (uint64_t)kernelBackbuf + i * 4096;
            MMU::MapPage(user_video_virt + i * 4096, 
                         phys_addr, 
                         PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        }
        
        // Flush TLB
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
        
        UART::Write("[Syscall] SYS_VIDEO_MAP: Mapped kernel backbuffer to user @ ");
        UART::WriteHex(user_video_virt);
        UART::Write("\n");
        
        return user_video_virt;
    }

    case 51: // SYS_VIDEO_FLIP
    {
        // Apps draw to their window buffer. Desktop is the compositor.
        // Apps should NOT trigger full Compose+Flip as that overwrites Desktop's scene.
        // Instead, we just mark the screen dirty so Desktop's next frame picks up changes.
        //
        // NOTE: Desktop's main loop calls its own SwapBuffersRect which handles
        // reading all window layers and presenting them.
        
        uint32_t w = Graphics::GetWidth();
        uint32_t h = Graphics::GetHeight();
        
        // Mark entire screen dirty (Desktop will see this)
        Compositor::MarkDirty(0, 0, w, h);
        
        // DO NOT call Compose+Flip here - Desktop handles that
        // Compositor::Compose();
        // Compositor::Flip();
        
        return 0;
    }

    case 54: // SYS_VIDEO_FLIP_RECT
    {
        // Present a rectangle from userspace backbuffer to the framebuffer.
        // arg1: backbuffer pointer (tightly packed width*height BGRA32)
        // arg2: (x<<32) | y
        // arg3: (w<<32) | h

        if (arg1 == 0)
            return 0;

        const uint64_t USER_SPACE_MIN = 0x600000000000ULL;
        if (arg1 < USER_SPACE_MIN)
            return 0;

        uint32_t* dest = Graphics::GetFramebuffer();
        if (!dest)
            return 0;

        uint32_t fb_w = Graphics::GetWidth();
        uint32_t fb_h = Graphics::GetHeight();
        uint32_t pitch = Graphics::GetPitch();

        uint32_t x = (uint32_t)(arg2 >> 32);
        uint32_t y = (uint32_t)(arg2 & 0xFFFFFFFFULL);
        uint32_t w = (uint32_t)(arg3 >> 32);
        uint32_t h = (uint32_t)(arg3 & 0xFFFFFFFFULL);

        if (fb_w == 0 || fb_h == 0) return 0;

        // Clamp / reject empty
        if (x >= fb_w || y >= fb_h) return 0;
        if (w == 0 || h == 0) return 0;
        if (x + w > fb_w) w = fb_w - x;
        if (y + h > fb_h) h = fb_h - y;

        uint32_t* src = (uint32_t*)arg1;

        // VSync wait with short timeout for smooth rendering
        // 2ms timeout prevents hanging in VNC while still reducing tearing
        bool vsynced = WaitVBlankTimeoutMs(2);

        // Copy only the rectangle.
        for (uint32_t row = 0; row < h; row++)
        {
            uint64_t dst_off = (uint64_t)(y + row) * pitch + x;
            uint64_t src_off = (uint64_t)(y + row) * fb_w + x;
            blit_fast_32(&dest[dst_off], &src[src_off], w);
        }

        return vsynced ? 1 : 0;
    }

    case 52: // SYS_INPUT_POLL
        // Arg1 = Pointer to InputEvent struct
        // Logic: Pop from input queue and copy to user buffer
        // For now, return 0 (no event)
        // NOTE: We assume userspace uses sys_get_event instead.
        return 0;

    case 65: // SYS_POST_MESSAGE (targetPid, eventPtr)
    {
        uint64_t targetPid = arg1;
        uint64_t msgPtr = arg2;
        if (msgPtr < 0x600000000000ULL) return 0;
        
        OSEvent ev = *(OSEvent*)msgPtr; // Copy from user
        Scheduler::PushEventToTask(targetPid, ev);
        return 1;
    }
    
    case 66: // SYS_COMPOSE_LAYERS
        Compositor::ComposeAppWindowsOnly();
        return 0;

    case 67: // SYS_GET_WINDOW_LIST (buffer, max)
        return Compositor::GetWindowList((void*)arg1, (uint32_t)arg2);

    case 68: // SYS_UPDATE_WINDOW (id, pos, size)
        Compositor::UpdateWindow(arg1, 
            (uint32_t)(arg2 >> 32), (uint32_t)(arg2 & 0xFFFFFFFF),
            (uint32_t)(arg3 >> 40), (uint32_t)((arg3 >> 16) & 0xFFFFFF),
            (uint32_t)(arg3 & 0xFFFF));
        return 0;

    case 70: // SYS_SET_KEYMAP
    {
        uint64_t ptr = arg1;
        if (ptr < 0x600000000000ULL) return 0; // Sanity check user pointer
        
        char id[4];
        char* user_id = (char*)ptr;
        // Copy safely
        for(int i=0; i<3; i++) id[i] = user_id[i];
        id[3] = 0;
        
        if (KeymapHAL::SetKeymap(id)) return 1;
        return 0;
    }

    case 62: // SYS_CREATE_WINDOW (width, height, flags)
    {
        UART::Write("[SYS_CREATE_WINDOW] Entry - caller task: ");
        UART::WriteDec(Scheduler::GetCurrentTaskId());
        UART::Write(", w=");
        UART::WriteDec(arg1);
        UART::Write(", h=");
        UART::WriteDec(arg2);
        UART::Write("\n");
        
        uint32_t w = (uint32_t)arg1;
        uint32_t h = (uint32_t)arg2;
        uint32_t flags = (uint32_t)arg3;

        // Validation
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return 0;

        // Create Window Layer (Physical Buffer allocated)
        Compositor::Window* win = Compositor::CreateWindow(w, h, flags);
        if (!win) return 0;
        
        UART::Write("[Syscall] Window Created ID: ");
        UART::WriteDec(win->id);
        UART::Write("\n");
        
        // Map to User Space (Unique per-window: Base + WindowID * 16MB)
        uint64_t user_video_virt = 0x600100000000ULL + (win->id * 0x1000000ULL);
        uint64_t pages = ((uint64_t)w * h * 4 + 4095) / 4096;
        
        for (uint64_t i = 0; i < pages; i++) {
             MMU::MapPage(user_video_virt + i * 4096, 
                          win->phys_addr + i * 4096, 
                          PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        }
        
        // Flush TLB
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
        
        return user_video_virt;
    }

    case SYS_SPAWN: {
        const char* user_path = (const char*)arg1;
        // Copy path to kernel stack buffer (256 chars max)
        // This is crucial because user_path is in the Caller's address space (e.g. 0x6000...)
        // Once we switch CR3 to the new process, that address space is GONE.
        char path_buf[256];
        int i = 0;
        while (i < 255) {
            path_buf[i] = user_path[i];
            if (path_buf[i] == 0) break;
            i++;
        }
        path_buf[i] = 0; // Ensure null termination
        
        // 1. Create a new Process Address Space (PML4)
        uint64_t newCR3 = MMU::CreatePageTable();
        if (!newCR3) {
            UART::Write("[Syscall] Spawn failed: Cannot create Page Table\n");
            return -1;
        }
        
        // 2. Switch to new Address Space temporarily
        // CRITICAL: Disable Interrupts to prevent Scheduler from preempting us
        // and restoring the old CR3 (since current_task->cr3 is still oldCR3).
        bool ints_enabled = HAL::Platform::AreInterruptsEnabled();
        HAL::Platform::DisableInterrupts();
        
        uint64_t oldCR3 = MMU::GetCurrentPageTable();
        MMU::SwitchPageTable(newCR3);
        
        // 3. Load App at FIXED Base Address (Standard Morphic App Base)
        // Since we are in a new address space, we can reuse 0x600000000000
        const uint64_t APP_BASE = 0x600000000000ULL;
        // Use path_buf which is on Kernel Stack (mapped in Identity/Kernel Space, so visible in newCR3)
        LoadedProcess proc = PackageLoader::Load(path_buf, APP_BASE);
        
        // 4. Switch back to Kernel/Caller Address Space
        MMU::SwitchPageTable(oldCR3);
        
        if (ints_enabled) HAL::Platform::EnableInterrupts();
        
        if (proc.error_code == 0) {
            // 5. Register Task with the NEW CR3
            // The Scheduler will switch to newCR3 when running this task
            Scheduler::CreateUserTask((void(*)())proc.entry_point, (void*)proc.stack_top, newCR3);
            return 0; // Success
        } else {
            // Failed. Cleanup Page Table? (Memory Leak TODO: DestroyPageTable)
            UART::Write("[Syscall] Spawn failed: Loader error ");
            UART::WriteDec(proc.error_code);
            UART::Write("\n");
            return (uint64_t)proc.error_code;
        }
    }

    case SYS_DEBUG_PRINT:
        UART::Write((const char*)arg1);
        return 0;

    case SYS_REGISTER_COMPOSITOR:
        // arg1: Unused (Registers Current Process)
        InputManager::SetCompositorPID(Scheduler::GetCurrentTaskId());
        Compositor::EnableUserspaceMode();
        // Disable kernel terminal output - Desktop now owns the display
        EarlyTerm::Disable();
        return 0;

    case SYS_MAP_WINDOW:
    {
        // arg1: WindowID
        uint64_t winId = arg1;
        
        Compositor::Window* win = Compositor::GetWindow(winId);
        if (!win) return 0;

        // Calculate size
        uint64_t sizeBytes = (uint64_t)win->width * win->height * 4;
        uint64_t pages = (sizeBytes + 4095) / 4096;

        // Find a free virtual address in user space (Simple increment for now)
        // Ideally we need a User Virtual Allocator
        // HACK: Use a fixed high range for Compositor mappings: 0x7000_0000_0000 + (ID * 16MB)
        uint64_t compos_base = 0x700000000000ULL + (winId * 0x1000000); // 16MB per window max slot
        
        for (uint64_t i = 0; i < pages; i++) {
             MMU::MapPage(compos_base + i * 4096, 
                          win->phys_addr + i * 4096, 
                          PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT);
        }
        
        // Flush TLB (lazy)
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");

        return compos_base;
    }



    default:
        return (uint64_t)-1;
    }
}
