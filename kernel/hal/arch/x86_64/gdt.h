#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct GDTDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// TSS Descriptor is 128-bit in x86_64 (uses 2 GDT entries)
struct TSSDescriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle1;
    uint8_t  access;
    uint8_t  flags_limit;
    uint8_t  base_middle2;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct GDTR {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

class GDT {
public:
    static void Init();
    static void LoadTSS(void* tss_ptr);

private:
    // 7 entries: Null, KCode, KData, UCode, UData, TSS(2 entries)
    static uint8_t gdt[56];  // 7 * 8 = 56 bytes
    static GDTR gdtr;
};

// Assembly functions
extern "C" void gdt_flush(uint64_t gdtr_addr);
extern "C" void tss_load(uint16_t selector);

#endif // GDT_H
