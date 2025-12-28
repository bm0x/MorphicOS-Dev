#pragma once

// UART Hardware Abstraction Layer
// Generic serial port interface for debugging

#include <stdint.h>

// UART configuration
struct UARTConfig {
    uint32_t baud_rate;     // Default: 115200
    uint8_t data_bits;      // 5, 6, 7, or 8
    uint8_t stop_bits;      // 1 or 2
    uint8_t parity;         // 0=None, 1=Odd, 2=Even
    bool flow_control;      // Hardware flow control
};

// Default configuration
#define UART_CONFIG_DEFAULT { 115200, 8, 1, 0, false }

// UART output targets
enum SerialOutput {
    OUTPUT_NONE   = 0,
    OUTPUT_SCREEN = 1,
    OUTPUT_SERIAL = 2,
    OUTPUT_BOTH   = 3
};

namespace UART {
    // Initialize UART with default config (COM1 on x86)
    void Init();
    
    // Initialize with custom config
    void InitWithConfig(uint32_t port_base, const UARTConfig* config);
    
    // Basic I/O
    void PutChar(char c);
    char GetChar();
    bool HasData();
    
    // String output
    void Write(const char* str);
    void WriteHex(uint64_t value);
    void WriteDec(int64_t value);
    
    // Line output with newline
    void WriteLine(const char* str);
    
    // Check if UART is ready for transmission
    bool IsReady();
    
    // Get port base address
    uint32_t GetPortBase();
}

// Global output control for verbose
namespace SerialDebug {
    void SetOutput(SerialOutput output);
    SerialOutput GetOutput();
    
    // Print to current output target(s)
    void Print(const char* str);
    void PrintHex(uint64_t value);
    void PrintDec(int64_t value);
}
