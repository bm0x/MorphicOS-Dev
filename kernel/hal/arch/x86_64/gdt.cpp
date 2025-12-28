#include "gdt.h"
#include "tss.h"
#include "../../video/early_term.h"

uint8_t GDT::gdt[56];
GDTR GDT::gdtr;

// Helper to set a regular GDT entry
static void SetGDTEntry(uint8_t* gdt, int index, uint8_t access, uint8_t gran) {
    GDTDescriptor* entry = (GDTDescriptor*)(gdt + index * 8);
    entry->limit_low = 0xFFFF;
    entry->base_low = 0;
    entry->base_middle = 0;
    entry->access = access;
    entry->granularity = gran;
    entry->base_high = 0;
}

void GDT::Init() {
    // 0: Null Descriptor
    for (int i = 0; i < 8; i++) gdt[i] = 0;
    
    // 1: Kernel Code (0x08) - Ring 0, Execute/Read
    SetGDTEntry(gdt, 1, 0x9A, 0xAF);
    
    // 2: Kernel Data (0x10) - Ring 0, Read/Write
    SetGDTEntry(gdt, 2, 0x92, 0xCF);
    
    // 3: User Code (0x18) - Ring 3, Execute/Read
    SetGDTEntry(gdt, 3, 0xFA, 0xAF);
    
    // 4: User Data (0x20) - Ring 3, Read/Write
    SetGDTEntry(gdt, 4, 0xF2, 0xCF);
    
    // 5-6: TSS (0x28) - Will be set by LoadTSS()
    for (int i = 40; i < 56; i++) gdt[i] = 0;
    
    // Load GDT
    gdtr.limit = 56 - 1;
    gdtr.base = (uint64_t)&gdt[0];
    
    gdt_flush((uint64_t)&gdtr);
}

void GDT::LoadTSS(void* tss_ptr) {
    uint64_t base = (uint64_t)tss_ptr;
    uint16_t limit = sizeof(TSS64) - 1;
    
    // TSS Descriptor at index 5 (offset 40) - 16 bytes
    TSSDescriptor* tss_desc = (TSSDescriptor*)(gdt + 40);
    
    tss_desc->limit_low = limit & 0xFFFF;
    tss_desc->base_low = base & 0xFFFF;
    tss_desc->base_middle1 = (base >> 16) & 0xFF;
    tss_desc->access = 0x89;  // Present, 64-bit TSS Available
    tss_desc->flags_limit = ((limit >> 16) & 0x0F);
    tss_desc->base_middle2 = (base >> 24) & 0xFF;
    tss_desc->base_high = (base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved = 0;
    
    // Reload GDT with TSS included
    gdtr.limit = 56 - 1;
    gdtr.base = (uint64_t)&gdt[0];
    gdt_flush((uint64_t)&gdtr);
    
    // Load TSS register (selector 0x28)
    tss_load(0x28);
    
    EarlyTerm::Print("[GDT] TSS loaded at selector 0x28\n");
}
