#include "idt.h"

IDTEntry IDT::idt[256];
IDTR IDT::idtr;

void IDT::Init() {
    uint64_t idt_address = (uint64_t)&idt[0];
    // isr_stub_table is defined in asm, we need to declare it properly in header or here
    // In header we did: extern "C" void* isr_stub_table[];

    // Fill first 32 ISRs (Exceptions)
    for (int i = 0; i < 32; i++) {
        uint64_t isr_addr = (uint64_t)isr_stub_table[i];
        SetGate(i, isr_addr, 0x08, 0x8E);
    }

    // Fill IRQs (32-47)
    for (int i = 32; i < 48; i++) {
        uint64_t isr_addr = (uint64_t)isr_stub_table[i];
        SetGate(i, isr_addr, 0x08, 0x8E);
    }

    
    // Load IDT
    idtr.limit = sizeof(IDTEntry) * 256 - 1;
    idtr.base = idt_address;
    
    idt_flush((uint64_t)&idtr);
}

void IDT::SetGate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_middle = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

void IDT::EnableInterrupts() {
    __asm__ volatile ("sti");
}

void IDT::DisableInterrupts() {
    __asm__ volatile ("cli");
}
