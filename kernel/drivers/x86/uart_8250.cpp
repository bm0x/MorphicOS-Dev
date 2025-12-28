// Intel 8250/16550 UART Driver for x86
// COM1-COM4 serial port support

#include "../../hal/serial/uart.h"
#include "../../hal/arch/x86_64/io.h"

// Standard COM port addresses
#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define COM3_BASE 0x3E8
#define COM4_BASE 0x2E8

// UART register offsets
#define UART_DATA     0   // Data register (read/write)
#define UART_IER      1   // Interrupt Enable Register
#define UART_FCR      2   // FIFO Control Register (write)
#define UART_IIR      2   // Interrupt ID Register (read)
#define UART_LCR      3   // Line Control Register
#define UART_MCR      4   // Modem Control Register
#define UART_LSR      5   // Line Status Register
#define UART_MSR      6   // Modem Status Register
#define UART_SCR      7   // Scratch Register

// Line Status Register bits
#define LSR_DATA_READY    0x01
#define LSR_OVERRUN_ERR   0x02
#define LSR_PARITY_ERR    0x04
#define LSR_FRAMING_ERR   0x08
#define LSR_BREAK_INT     0x10
#define LSR_TX_EMPTY      0x20
#define LSR_TX_IDLE       0x40
#define LSR_FIFO_ERR      0x80

namespace UART {
    static uint32_t activePort = COM1_BASE;
    static bool initialized = false;
    
    void Init() {
        UARTConfig config = UART_CONFIG_DEFAULT;
        InitWithConfig(COM1_BASE, &config);
    }
    
    void InitWithConfig(uint32_t port_base, const UARTConfig* config) {
        activePort = port_base;
        
        // Disable interrupts
        IO::outb(port_base + UART_IER, 0x00);
        
        // Enable DLAB (Divisor Latch Access Bit) to set baud rate
        IO::outb(port_base + UART_LCR, 0x80);
        
        // Set baud rate divisor
        // Divisor = 115200 / baud_rate
        uint16_t divisor = 115200 / config->baud_rate;
        IO::outb(port_base + UART_DATA, divisor & 0xFF);         // Low byte
        IO::outb(port_base + UART_IER, (divisor >> 8) & 0xFF);   // High byte
        
        // Configure line: 8 bits, no parity, 1 stop bit (8N1)
        uint8_t lcr = 0;
        lcr |= (config->data_bits - 5) & 0x03;  // Data bits
        if (config->stop_bits == 2) lcr |= 0x04;
        if (config->parity == 1) lcr |= 0x08;       // Odd parity
        else if (config->parity == 2) lcr |= 0x18;  // Even parity
        IO::outb(port_base + UART_LCR, lcr);
        
        // Enable FIFO, clear buffers, 14-byte threshold
        IO::outb(port_base + UART_FCR, 0xC7);
        
        // Enable IRQs, RTS/DSR set
        IO::outb(port_base + UART_MCR, 0x0B);
        
        // Test the chip with loopback mode
        IO::outb(port_base + UART_MCR, 0x1E);  // Enable loopback
        IO::outb(port_base + UART_DATA, 0xAE); // Send test byte
        
        if (IO::inb(port_base + UART_DATA) != 0xAE) {
            // Serial port failed, unusable
            initialized = false;
            return;
        }
        
        // Disable loopback, enable normal operation
        IO::outb(port_base + UART_MCR, 0x0F);
        
        initialized = true;
    }
    
    bool IsReady() {
        return initialized && (IO::inb(activePort + UART_LSR) & LSR_TX_EMPTY);
    }
    
    void PutChar(char c) {
        if (!initialized) return;
        
        // Wait for transmit buffer empty
        while (!(IO::inb(activePort + UART_LSR) & LSR_TX_EMPTY));
        
        IO::outb(activePort + UART_DATA, c);
    }
    
    char GetChar() {
        if (!initialized) return 0;
        
        // Wait for data ready
        while (!(IO::inb(activePort + UART_LSR) & LSR_DATA_READY));
        
        return IO::inb(activePort + UART_DATA);
    }
    
    bool HasData() {
        return initialized && (IO::inb(activePort + UART_LSR) & LSR_DATA_READY);
    }
    
    void Write(const char* str) {
        if (!initialized || !str) return;
        
        while (*str) {
            if (*str == '\n') PutChar('\r');  // CR before LF
            PutChar(*str++);
        }
    }
    
    void WriteHex(uint64_t value) {
        const char* hex = "0123456789ABCDEF";
        char buffer[17];
        buffer[16] = 0;
        
        for (int i = 15; i >= 0; i--) {
            buffer[i] = hex[value & 0xF];
            value >>= 4;
        }
        
        Write("0x");
        // Skip leading zeros
        int start = 0;
        while (start < 15 && buffer[start] == '0') start++;
        Write(&buffer[start]);
    }
    
    void WriteDec(int64_t value) {
        if (value < 0) {
            PutChar('-');
            value = -value;
        }
        
        char buffer[21];
        int i = 20;
        buffer[i] = 0;
        
        if (value == 0) {
            buffer[--i] = '0';
        } else {
            while (value > 0) {
                buffer[--i] = '0' + (value % 10);
                value /= 10;
            }
        }
        
        Write(&buffer[i]);
    }
    
    void WriteLine(const char* str) {
        Write(str);
        Write("\n");
    }
    
    uint32_t GetPortBase() {
        return activePort;
    }
}

// Serial debug output control
namespace SerialDebug {
    static SerialOutput currentOutput = OUTPUT_SCREEN;
    
    void SetOutput(SerialOutput output) {
        currentOutput = output;
    }
    
    SerialOutput GetOutput() {
        return currentOutput;
    }
    
    void Print(const char* str) {
        if (currentOutput & OUTPUT_SERIAL) {
            UART::Write(str);
        }
        // Screen output handled by EarlyTerm
    }
    
    void PrintHex(uint64_t value) {
        if (currentOutput & OUTPUT_SERIAL) {
            UART::WriteHex(value);
        }
    }
    
    void PrintDec(int64_t value) {
        if (currentOutput & OUTPUT_SERIAL) {
            UART::WriteDec(value);
        }
    }
}
