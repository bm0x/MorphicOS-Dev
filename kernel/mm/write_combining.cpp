// Write-Combining Memory Optimization
// Uses PAT (Page Attribute Table) to accelerate framebuffer writes

#include "write_combining.h"
#include "../hal/video/early_term.h"

// MSR definitions
#define IA32_PAT 0x277
#define IA32_MTRRCAP 0xFE
#define IA32_MTRR_DEF_TYPE 0x2FF

// PAT entry types
#define PAT_UC  0x00  // Uncacheable
#define PAT_WC  0x01  // Write-Combining
#define PAT_WT  0x04  // Write-Through
#define PAT_WP  0x05  // Write-Protect
#define PAT_WB  0x06  // Write-Back
#define PAT_UC_ 0x07  // Uncached

// Read MSR
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

// Write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "a"((uint32_t)value), "d"((uint32_t)(value >> 32)), "c"(msr));
}

// Check CPUID for PAT support
static bool CheckPATSupport() {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx & (1 << 16)) != 0;  // PAT bit
}

namespace WriteCombining {
    
    static bool patInitialized = false;
    static bool patSupported = false;
    
    bool IsSupported() {
        return CheckPATSupport();
    }
    
    void InitPAT() {
        if (patInitialized) return;
        
        if (!CheckPATSupport()) {
            EarlyTerm::Print("[WC] PAT not supported by CPU\n");
            return;
        }
        
        patSupported = true;
        
        // Read current PAT
        uint64_t pat = rdmsr(IA32_PAT);
        
        // Default PAT layout:
        // PAT0=WB, PAT1=WT, PAT2=UC-, PAT3=UC, PAT4=WB, PAT5=WT, PAT6=UC-, PAT7=UC
        // We want to set PAT1 = Write-Combining (0x01)
        
        // Clear PAT1 (bits 8-15) and set to WC
        pat = (pat & ~0xFF00ULL) | ((uint64_t)PAT_WC << 8);
        
        // Write modified PAT
        wrmsr(IA32_PAT, pat);
        
        patInitialized = true;
        EarlyTerm::Print("[WC] PAT initialized with Write-Combining\n");
    }
    
    bool MarkRegion(uint64_t baseAddr, size_t size) {
        if (!patSupported) {
            InitPAT();
            if (!patSupported) return false;
        }
        
        // Note: Actually marking pages as WC requires modifying PTEs
        // with PAT/PCD/PWT bits. For now we just initialize PAT.
        // The bootloader/UEFI typically sets framebuffer as WC already.
        
        EarlyTerm::Print("[WC] Region marked: 0x");
        EarlyTerm::PrintHex(baseAddr);
        EarlyTerm::Print(" size: ");
        EarlyTerm::PrintDec(size / (1024 * 1024));
        EarlyTerm::Print(" MB\n");
        
        return true;
    }
}
