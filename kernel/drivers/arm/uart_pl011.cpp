// ARM PL011 UART Driver Stub
// For ARM-based SoCs (Raspberry Pi, QEMU virt, etc.)

#include "../../hal/serial/uart.h"

// NOTE: This is a stub for future ARM64 port
// PL011 is memory-mapped, not port I/O

// PL011 register offsets (from base address)
#define PL011_DR     0x000  // Data Register
#define PL011_RSR    0x004  // Receive Status Register
#define PL011_FR     0x018  // Flag Register
#define PL011_IBRD   0x024  // Integer Baud Rate Divisor
#define PL011_FBRD   0x028  // Fractional Baud Rate Divisor
#define PL011_LCR_H  0x02C  // Line Control Register
#define PL011_CR     0x030  // Control Register
#define PL011_IMSC   0x038  // Interrupt Mask Set/Clear

// Flag register bits
#define FR_TXFF      (1 << 5)  // Transmit FIFO full
#define FR_RXFE      (1 << 4)  // Receive FIFO empty

#ifdef __aarch64__

namespace UART {
    static volatile uint32_t* uartBase = nullptr;
    static bool initialized = false;
    
    static inline void mmio_write(uint32_t offset, uint32_t value) {
        if (uartBase) {
            uartBase[offset / 4] = value;
        }
    }
    
    static inline uint32_t mmio_read(uint32_t offset) {
        return uartBase ? uartBase[offset / 4] : 0;
    }
    
    void Init() {
        // Default PL011 address for QEMU virt machine
        uartBase = (volatile uint32_t*)0x09000000;
        
        // Disable UART
        mmio_write(PL011_CR, 0);
        
        // Clear pending interrupts
        mmio_write(PL011_IMSC, 0);
        
        // Set baud rate to 115200 (assuming 24MHz clock)
        // IBRD = 24000000 / (16 * 115200) = 13
        // FBRD = round((0.020833 * 64) + 0.5) = 1
        mmio_write(PL011_IBRD, 13);
        mmio_write(PL011_FBRD, 1);
        
        // 8 bits, no parity, 1 stop, FIFO enabled
        mmio_write(PL011_LCR_H, (1 << 4) | (1 << 5) | (1 << 6));
        
        // Enable UART, TX, RX
        mmio_write(PL011_CR, (1 << 0) | (1 << 8) | (1 << 9));
        
        initialized = true;
    }
    
    void InitWithConfig(uint32_t port_base, const UARTConfig* config) {
        uartBase = (volatile uint32_t*)(uint64_t)port_base;
        Init();
    }
    
    bool IsReady() {
        return initialized && !(mmio_read(PL011_FR) & FR_TXFF);
    }
    
    void PutChar(char c) {
        if (!initialized) return;
        while (mmio_read(PL011_FR) & FR_TXFF);
        mmio_write(PL011_DR, c);
    }
    
    char GetChar() {
        if (!initialized) return 0;
        while (mmio_read(PL011_FR) & FR_RXFE);
        return mmio_read(PL011_DR) & 0xFF;
    }
    
    bool HasData() {
        return initialized && !(mmio_read(PL011_FR) & FR_RXFE);
    }
    
    void Write(const char* str) {
        while (*str) {
            if (*str == '\n') PutChar('\r');
            PutChar(*str++);
        }
    }
    
    void WriteHex(uint64_t value) {
        const char* hex = "0123456789ABCDEF";
        Write("0x");
        for (int i = 60; i >= 0; i -= 4) {
            PutChar(hex[(value >> i) & 0xF]);
        }
    }
    
    void WriteDec(int64_t value) {
        if (value < 0) { PutChar('-'); value = -value; }
        if (value == 0) { PutChar('0'); return; }
        char buf[21]; int i = 20; buf[i] = 0;
        while (value > 0) { buf[--i] = '0' + (value % 10); value /= 10; }
        Write(&buf[i]);
    }
    
    void WriteLine(const char* str) { Write(str); Write("\n"); }
    uint32_t GetPortBase() { return (uint32_t)(uint64_t)uartBase; }
}

namespace SerialDebug {
    static SerialOutput currentOutput = OUTPUT_SERIAL;  // ARM defaults to serial
    void SetOutput(SerialOutput o) { currentOutput = o; }
    SerialOutput GetOutput() { return currentOutput; }
    void Print(const char* s) { if (currentOutput & OUTPUT_SERIAL) UART::Write(s); }
    void PrintHex(uint64_t v) { if (currentOutput & OUTPUT_SERIAL) UART::WriteHex(v); }
    void PrintDec(int64_t v) { if (currentOutput & OUTPUT_SERIAL) UART::WriteDec(v); }
}

#endif // __aarch64__
