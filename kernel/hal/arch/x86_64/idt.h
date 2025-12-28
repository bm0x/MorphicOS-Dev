#ifndef IDT_H
#define IDT_H

#include <stdint.h>

struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;     // Interrupt Stack Table offset (0 for now)
    uint8_t  type_attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTR {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

class IDT {
public:
    static void Init();
    static void SetGate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
    static void EnableInterrupts();
    static void DisableInterrupts();

private:
    static IDTEntry idt[256];
    static IDTR idtr;
};

// Defined in interrupts.asm
extern "C" void idt_flush(uint64_t idtr_ptr);
extern "C" void* isr_stub_table[];

#endif // IDT_H
