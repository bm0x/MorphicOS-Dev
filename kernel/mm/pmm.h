#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "../utils/bitmap.h"
#include "../../shared/boot_info.h"

class PMM {
public:
    static void Init(BootInfo* bootInfo);
    
    // Allocate a single page (4KB)
    static void* ParamAllocPage(); // 'AllocPage' might conflict if not careful, sticking to Alloc
    static void* AllocPage();
    
    static void FreePage(void* address);
    
    static size_t GetTotalMemory();
    static size_t GetFreeMemory();

private:
    static Bitmap bitmap;
    static size_t totalRAM;
    static size_t freeRAM;
    static size_t bitmapSize;
    static uint64_t highestAddr;
};

#endif // PMM_H
