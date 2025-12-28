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
                bitmap.Set(startPage + p, false); // Free
                freeRAM += 4096;
            }
        }
    }


    // 6. Protect Critical Regions (Mark as Used)
    
    // A. Bitmap Itself
    uint64_t bitmapStartPage = (uint64_t)bitmapBuffer / 4096;
    uint64_t bitmapPages = (bitmapSize + 4095) / 4096;
    for (uint64_t p = 0; p < bitmapPages; p++) {
        if (!bitmap.Get(bitmapStartPage + p)) {
            bitmap.Set(bitmapStartPage + p, true);
            freeRAM -= 4096;
        }
    }

    // B. Kernel (0 - 2MB)
    // We protect the first 2MB to cover the Kernel and Trampoline code.
    // Ideally we should parse the Linker Map or pass Kernel size from Bootloader.
    for (uint64_t p = 0; p < (0x200000 / 4096); p++) {
        if (!bitmap.Get(p)) {
             bitmap.Set(p, true);
             // Don't subtract freeRAM here because it probably wasn't counted as Conventionl if BIOS reserved it, 
             // but if it WAS Conventional (LoaderCode), we reclaim it as Used.
             // If we already counted it as free in Step 5 (because LoaderCode was technically available?), 
             // wait, we only freed ConventionalMemory. Kernel is likely in LoaderData/Code or Conventional.
             // If Kernel is in Conventional, we just marked it Free. So we must subtract.
             // To be safe, we check if we are flipping it back.
             // Our Step 5 ONLY checks ConventionalMemory. 
             // If Kernel area was marked Conventional by BIOS (and then used by Bootloader), 
             // we marked it Free. So yes, subtract.
             if (freeRAM >= 4096) freeRAM -= 4096;
        }
    }
}

void* PMM::AllocPage() {
    uint64_t limit = highestAddr / 4096;
    for (uint64_t i = 0; i < limit; i++) {
        if (!bitmap.Get(i)) {
            bitmap.Set(i, true);
            freeRAM -= 4096;
            return (void*)(i * 4096);
        }
    }
    return nullptr;
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
