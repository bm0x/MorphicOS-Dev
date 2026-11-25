#include "pmm.h"
#include "../arch/common/spinlock.h"
#include "../hal/video/early_term.h"

extern "C" uint8_t _kernel_start[];
extern "C" uint8_t _kernel_end[];

static Spinlock pmmLock = SPINLOCK_INIT;

// Static members
Bitmap PMM::bitmap;
size_t PMM::totalRAM = 0;
size_t PMM::freeRAM = 0;
size_t PMM::bitmapSize = 0;
uint64_t PMM::highestAddr = 0;

// Helper: Check if memory type is usable
static bool IsUsableMemory(uint32_t type) {
    return type == (uint32_t)MemoryType::ConventionalMemory ||
           type == (uint32_t)MemoryType::BootServicesCode ||
           type == (uint32_t)MemoryType::BootServicesData ||
           type == (uint32_t)MemoryType::LoaderCode ||
           type == (uint32_t)MemoryType::LoaderData;
}

void PMM::Init(BootInfo* bootInfo) {
    MemoryDescriptor* map = bootInfo->memoryMap;
    uint64_t mapEntries = bootInfo->memoryMapSize / bootInfo->memoryMapDescriptorSize;
    uint64_t descriptorSize = bootInfo->memoryMapDescriptorSize;

    // 1. Calculate Highest Address from usable memory
    highestAddr = 0;
    for (uint64_t i = 0; i < mapEntries; i++) {
        MemoryDescriptor* desc = (MemoryDescriptor*)((uint8_t*)map + i * descriptorSize);
        uint64_t regionEnd = desc->physAddr + (desc->numPages * 4096);
        if (regionEnd > highestAddr) {
            highestAddr = regionEnd;
        }
    }
    
    // Sanity check: ensure we have at least 16MB addressable
    if (highestAddr < 0x1000000) {
        highestAddr = 0x1000000;
    }

    // 2. Calculate Bitmap Size
    size_t totalPages = highestAddr / 4096;
    bitmapSize = (totalPages / 8) + 1;

    // 3. Find space for Bitmap - search all usable memory types
    void* bitmapBuffer = nullptr;
    for (uint64_t i = 0; i < mapEntries && !bitmapBuffer; i++) {
        MemoryDescriptor* desc = (MemoryDescriptor*)((uint8_t*)map + i * descriptorSize);
        if (IsUsableMemory(desc->type) && desc->numPages * 4096 >= bitmapSize) {
            // Skip first 1MB to avoid BIOS/firmware areas
            if (desc->physAddr >= 0x100000) {
                bitmapBuffer = (void*)desc->physAddr;
            }
        }
    }
    
    // Fallback: try any usable memory
    if (!bitmapBuffer) {
        for (uint64_t i = 0; i < mapEntries; i++) {
            MemoryDescriptor* desc = (MemoryDescriptor*)((uint8_t*)map + i * descriptorSize);
            if (IsUsableMemory(desc->type) && desc->numPages * 4096 >= bitmapSize) {
                bitmapBuffer = (void*)desc->physAddr;
                break;
            }
        }
    }

    if (!bitmapBuffer) {
        EarlyTerm::Print("Panic: No usable RAM for PMM! Entries: ");
        EarlyTerm::PrintHex(mapEntries);
        EarlyTerm::Print("\n");
        // Print first few entries for debug
        for (uint64_t i = 0; i < mapEntries && i < 5; i++) {
            MemoryDescriptor* desc = (MemoryDescriptor*)((uint8_t*)map + i * descriptorSize);
            EarlyTerm::Print("  Type:");
            EarlyTerm::PrintHex(desc->type);
            EarlyTerm::Print(" Addr:");
            EarlyTerm::PrintHex(desc->physAddr);
            EarlyTerm::Print(" Pages:");
            EarlyTerm::PrintHex(desc->numPages);
            EarlyTerm::Print("\n");
        }
        while(1);
    }


    bitmap.Init(bitmapBuffer, bitmapSize);

    // 4. Mark Allocator as Full (Used) initially
    for (size_t i = 0; i < bitmapSize; i++) {
        ((uint8_t*)bitmapBuffer)[i] = 0xFF;
    }

    // 5. Mark Available Regions as Free
    totalRAM = 0;
    freeRAM = 0;

    for (uint64_t i = 0; i < mapEntries; i++) {
        MemoryDescriptor* desc = (MemoryDescriptor*)((uint8_t*)map + i * descriptorSize);
        
        // Count usable RAM (all usable types, not just ConventionalMemory)
        if (IsUsableMemory(desc->type)) {
            uint64_t startPage = desc->physAddr / 4096;
            uint64_t count = desc->numPages;
            
            totalRAM += count * 4096;
            
            for (uint64_t p = 0; p < count; p++) {
                // SLLOOOOW
                // bitmap.Set(startPage + p, false); // Free
            }
            bitmap.SetRange(startPage, count, false);
            freeRAM += count * 4096;
        }
    }


    // 6. Protect Critical Regions (Mark as Used with exact freeRAM accounting)
    // A. Bitmap Itself
    ReserveRange((uint64_t)bitmapBuffer, bitmapSize);

    // B. Kernel Low Reserved Zone (0 - 256MB)
    constexpr uint64_t LOW_RESERVED_SIZE = 0x10000000ULL; // 256MB
    ReserveRange(0, LOW_RESERVED_SIZE);
    
    // C. The Actual Kernel (Default 1GB region + linker symbols + relocated kernel if any)
    constexpr uint64_t KERNEL_PHYS_BASE = 0x40000000ULL;
    constexpr uint64_t KERNEL_SIZE_MAX  = 0x01000000ULL; // 16MB baseline guard
    ReserveRange(KERNEL_PHYS_BASE, KERNEL_SIZE_MAX);

    uint64_t kStart = (uint64_t)_kernel_start;
    uint64_t kEnd   = (uint64_t)_kernel_end;
    if (kEnd > kStart) {
        ReserveRange(kStart, kEnd - kStart);
    }
    if (bootInfo->kernelBasePhys != 0 && bootInfo->kernelSize != 0) {
        ReserveRange(bootInfo->kernelBasePhys, bootInfo->kernelSize);
    }

    // D. BootInfo, UEFI Memory Map, and InitRD (allocated as EfiLoaderData by bootloader)
    ReserveRange((uint64_t)bootInfo, sizeof(BootInfo));
    if (bootInfo->memoryMap != nullptr && bootInfo->memoryMapSize > 0) {
        ReserveRange((uint64_t)bootInfo->memoryMap, bootInfo->memoryMapSize);
    }
    if (bootInfo->initrdAddr != 0 && bootInfo->initrdSize > 0) {
        ReserveRange(bootInfo->initrdAddr, bootInfo->initrdSize);
    }
}

void PMM::ReserveRange(uint64_t physAddr, uint64_t byteSize) {
    if (byteSize == 0) return;
    uint64_t maxPages = highestAddr / 4096;
    uint64_t startPage = physAddr / 4096;
    if (startPage >= maxPages) return;

    uint64_t endAddr = physAddr + byteSize;
    if (endAddr < physAddr || endAddr > highestAddr) {
        endAddr = highestAddr;
    }
    uint64_t endPage = (endAddr + 4095) / 4096;
    if (endPage > maxPages) endPage = maxPages;

    for (uint64_t p = startPage; p < endPage; p++) {
        if (!bitmap.Get(p)) {
            bitmap.Set(p, true);
            if (freeRAM >= 4096) freeRAM -= 4096;
        }
    }
}

// Optimization: Start search from last known free index
static uint64_t lastFreeIndex = 0;

void* PMM::AllocPage() {
    CRITICAL_SECTION(pmmLock);
    uint64_t limit = highestAddr / 4096;
    constexpr uint64_t minPage = 0x10000000ULL / 4096; // Never hand out < 256MB
    uint64_t searchStart = (lastFreeIndex >= minPage) ? lastFreeIndex : minPage;
    
    // Quick search starting from hint
    for (uint64_t i = searchStart; i < limit; i++) {
        if (!bitmap.Get(i)) {
            bitmap.Set(i, true);
            if (freeRAM >= 4096) freeRAM -= 4096;
            lastFreeIndex = i + 1; // Update hint
            return (void*)(i * 4096);
        }
    }
    
    // Fallback: search from 256MB boundary
    for (uint64_t i = minPage; i < searchStart && i < limit; i++) {
        if (!bitmap.Get(i)) {
            bitmap.Set(i, true);
            if (freeRAM >= 4096) freeRAM -= 4096;
            lastFreeIndex = i + 1;
            return (void*)(i * 4096);
        }
    }
    
    return nullptr;
}

void* PMM::AllocContiguous(size_t pages) {
    if (pages == 0) return nullptr;
    CRITICAL_SECTION(pmmLock);

    // Find a contiguous block of 'pages' free pages
    // Start searching after the kernel reserved zone (256MB)
    uint64_t limit = highestAddr / 4096;
    if (pages > limit) return nullptr;
    uint64_t start_search = 0x10000000ULL / 4096; // Start at 256MB
    
    for (uint64_t start = start_search; start <= limit - pages; start++) {
        bool found = true;
        
        // Check if all pages in range are free
        for (uint64_t i = 0; i < pages && found; i++) {
            if (bitmap.Get(start + i)) {
                found = false;
                start = start + i; // Skip ahead to avoid rechecking
            }
        }
        
        if (found) {
            // Mark all pages as used
            for (uint64_t i = 0; i < pages; i++) {
                bitmap.Set(start + i, true);
                if (freeRAM >= 4096) freeRAM -= 4096;
            }
            if (start + pages > lastFreeIndex) lastFreeIndex = start + pages;
            return (void*)((start) * 4096);
        }
    }
    
    return nullptr; // No contiguous block found
}

void PMM::FreePage(void* address) {
    if (!address) return;
    CRITICAL_SECTION(pmmLock);
    uint64_t addr = (uint64_t)address;
    if (addr < 0x10000000ULL || addr >= highestAddr) return; // Protect reserved low zone
    uint64_t page = addr / 4096;
    if (bitmap.Get(page)) {
        bitmap.Set(page, false);
        freeRAM += 4096;
        if (page < lastFreeIndex) lastFreeIndex = page;
    }
}

void PMM::FreePages(void* address, size_t pages) {
    if (!address || pages == 0) return;
    uint64_t base = (uint64_t)address;
    for (size_t i = 0; i < pages; i++) {
        FreePage((void*)(base + i * 4096));
    }
}

size_t PMM::GetTotalMemory() { return totalRAM; }
size_t PMM::GetFreeMemory() { return freeRAM; }
