// Platform implementation for x86_64

#include "../../../../kernel/arch/common/platform.h"
#include "io.h"
#include "pic.h"
#include "../../video/early_term.h"


namespace Platform {
    void Init() {
        // PIC is already initialized in kernel_main via pic.h
        EarlyTerm::Print("[Platform] x86_64 initialized.\n");
    }

    
    void Halt() {
        __asm__ volatile("hlt");
    }
    
    void DisableInterrupts() {
        __asm__ volatile("cli");
    }
    
    void EnableInterrupts() {
        __asm__ volatile("sti");
    }
    
    void MemoryBarrier() {
        __asm__ volatile("mfence" ::: "memory");
    }
    
    void ReadBarrier() {
        __asm__ volatile("lfence" ::: "memory");
    }
    
    void WriteBarrier() {
        __asm__ volatile("sfence" ::: "memory");
    }
    
    void MaskIRQ(uint8_t irq) {
        uint16_t port;
        if (irq < 8) {
            port = 0x21;
        } else {
            port = 0xA1;
            irq -= 8;
        }
        uint8_t mask = IO::inb(port) | (1 << irq);
        IO::outb(port, mask);
    }
    
    void UnmaskIRQ(uint8_t irq) {
        uint16_t port;
        if (irq < 8) {
            port = 0x21;
        } else {
            port = 0xA1;
            irq -= 8;
        }
        uint8_t mask = IO::inb(port) & ~(1 << irq);
        IO::outb(port, mask);
    }
    
    void SendEOI(uint8_t irq) {
        if (irq >= 8) {
            IO::outb(0xA0, 0x20);  // Slave EOI
        }
        IO::outb(0x20, 0x20);      // Master EOI
    }
    
    const char* GetArchName() {
        return "x86_64";
    }
    
    uint32_t GetCPUFrequencyMHz() {
        return 0;  // TODO: detect via CPUID
    }
}
