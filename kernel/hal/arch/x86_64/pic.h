#ifndef PIC_H
#define PIC_H

#include <stdint.h>

class PIC {
public:
    static void Remap() {
        // Master PIC: 0x20 command, 0x21 data
        // Slave PIC:  0xA0 command, 0xA1 data
        
        // ICW1: Init
        outb(0x20, 0x11);
        io_wait();
        outb(0xA0, 0x11);
        io_wait();
        
        // ICW2: Vector Offset (32 = 0x20)
        outb(0x21, 0x20); // Master mapped to 0x20-0x27
        io_wait();
        outb(0xA1, 0x28); // Slave mapped to 0x28-0x2F
        io_wait();
        
        // ICW3: Cascading
        outb(0x21, 0x04);
        io_wait();
        outb(0xA1, 0x02);
        io_wait();
        
        // ICW4: Generic
        outb(0x21, 0x01); // 8086 mode
        io_wait();
        outb(0xA1, 0x01);
        io_wait();
        
        // Mask all for SAFETY until we have drivers
        outb(0x21, 0xFF);
        outb(0xA1, 0xFF);
    }

private:
    static inline void outb(uint16_t port, uint8_t val) {
        __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
    }
    
    static inline void io_wait() {
        // Port 0x80 is used for checkpoints
        outb(0x80, 0);
    }
};

#endif // PIC_H
