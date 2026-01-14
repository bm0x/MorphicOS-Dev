#include "pmm.h"
#include "../hal/video/early_term.h"

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


    // 6. Protect Critical Regions (Mark as Used)
    
    // A. Bitmap Itself
    uint64_t bitmapStartPage = (uint64_t)bitmapBuffer / 4096;
    uint64_t bitmapPages = (bitmapSize + 4095) / 4096;
    bitmap.SetRange(bitmapStartPage, bitmapPages, true);
    // freeRAM update is tricky with range, let's just ignore precise freeRAM count adjustment for reserved areas 
    // or recalculate. It's fine, strict accounting isn't critical for boot speed, safety is.
    // Actually we should subtract.
    // Approximating freeRAM subtraction:
    if (freeRAM >= bitmapPages * 4096) freeRAM -= bitmapPages * 4096;

    // B. Kernel Reserved Zone (0 - 256MB)
    constexpr uint64_t LOW_RESERVED_SIZE = 0x10000000; // 256MB
    uint64_t lowPages = LOW_RESERVED_SIZE / 4096;
    bitmap.SetRange(0, lowPages, true);
     if (freeRAM >= lowPages * 4096) freeRAM -= lowPages * 4096;
    
    // C. The Actual Kernel (Loaded at 1GB = 0x40000000)
    // We reserve 16MB for Kernel Code/Data/BSS/Stack
    constexpr uint64_t KERNEL_PHYS_BASE = 0x40000000;
    constexpr uint64_t KERNEL_SIZE_MAX = 0x01000000; // 16MB
    
    uint64_t kernelStartPage = KERNEL_PHYS_BASE / 4096;
    uint64_t kernelPages = KERNEL_SIZE_MAX / 4096;
    
    bitmap.SetRange(kernelStartPage, kernelPages, true);
    if (freeRAM >= kernelPages * 4096) freeRAM -= kernelPages * 4096;
}

// Optimization: Start search from last known free index
static uint64_t lastFreeIndex = 0;

void* PMM::AllocPage() {
    uint64_t limit = highestAddr / 4096;
    
    // Quick search starting from hint
    for (uint64_t i = lastFreeIndex; i < limit; i++) {
        if (!bitmap.Get(i)) {
            bitmap.Set(i, true);
            freeRAM -= 4096;
            lastFreeIndex = i + 1; // Update hint
            return (void*)(i * 4096);
        }
    }
    
    // Fallback: search from beginning
    for (uint64_t i = 0; i < lastFreeIndex; i++) {
        if (!bitmap.Get(i)) {
            bitmap.Set(i, true);
            freeRAM -= 4096;
            lastFreeIndex = i + 1;
            return (void*)(i * 4096);
        }
    }
    
    return nullptr;
}

void* PMM::AllocContiguous(size_t pages) {
    // Find a contiguous block of 'pages' free pages
    // Start searching after the kernel reserved zone (256MB)
    uint64_t limit = highestAddr / 4096;
    uint64_t start_search = 0x10000000 / 4096; // Start at 256MB
    
    // Optimization: Use lastFreeIndex if it's beyond reserved zone
    if (lastFreeIndex > start_search) start_search = lastFreeIndex;
    
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
                freeRAM -= 4096;
            }
            // Update hint
            if (start + pages > lastFreeIndex) lastFreeIndex = start + pages;
            return (void*)((start) * 4096);
        }
    }
    
    return nullptr; // No contiguous block found
}

void PMM::FreePage(void* address) {
    uint64_t page = (uint64_t)address / 4096;
    if (bitmap.Get(page)) {
        bitmap.Set(page, false);
        freeRAM += 4096;
    }
}

size_t PMM::GetTotalMemory() { return totalRAM; }
size_t PMM::GetFreeMemory() { return freeRAM; }
