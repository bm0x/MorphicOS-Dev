#include "panic.h"
#include "../hal/video/early_term.h"
#include "../arch/common/platform.h"
#include "../hal/arch/x86_64/io.h"

// Serial port (COM1)
#define COM1_PORT 0x3F8

namespace KernelPanic {
    static bool serialInitialized = false;
    static PanicInfo lastPanic;
    
    // Initialize COM1 serial port
    static void InitSerial() {
        IO::outb(COM1_PORT + 1, 0x00);    // Disable interrupts
        IO::outb(COM1_PORT + 3, 0x80);    // Enable DLAB
        IO::outb(COM1_PORT + 0, 0x03);    // 38400 baud (low byte)
        IO::outb(COM1_PORT + 1, 0x00);    //            (high byte)
        IO::outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, 1 stop
        IO::outb(COM1_PORT + 2, 0xC7);    // Enable FIFO
        IO::outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
        serialInitialized = true;
    }
    
    static bool SerialEmpty() {
        return (IO::inb(COM1_PORT + 5) & 0x20) != 0;
    }
    
    static void SerialPutChar(char c) {
        while (!SerialEmpty());
        IO::outb(COM1_PORT, c);
    }
    
    void Init() {
        InitSerial();
        EarlyTerm::Print("[Panic] System initialized (serial @ COM1)\n");
    }
    
    void WriteToScreen(const char* msg) {
        EarlyTerm::SetColor(0xFF0000, 0x000000);  // Red on black
        EarlyTerm::Print("\n!!! KERNEL PANIC !!!\n");
        EarlyTerm::Print(msg);
        EarlyTerm::Print("\n");
    }
    
    void WriteToSerial(const char* msg) {
        if (!serialInitialized) InitSerial();
        
        const char* prefix = "\r\n=== KERNEL PANIC ===\r\n";
        while (*prefix) SerialPutChar(*prefix++);
        
        while (*msg) {
            if (*msg == '\n') SerialPutChar('\r');
            SerialPutChar(*msg++);
        }
        
        SerialPutChar('\r');
        SerialPutChar('\n');
    }
    
    void WriteToVFS(const char* path) {
        // VFS write is risky during panic - skip for now
        // Could corrupt filesystem if panic happened during VFS operation
    }
    
    void Panic(const char* message, const char* file, int line) {
        // Disable interrupts immediately
        Platform::DisableInterrupts();
        
        // Store panic info
        lastPanic.message = message;
        lastPanic.file = file;
        lastPanic.line = line;
        
        // Output to screen
        WriteToScreen("------------------------------");
        EarlyTerm::Print("Message: ");
        EarlyTerm::Print(message);
        EarlyTerm::Print("\nFile: ");
        EarlyTerm::Print(file);
        EarlyTerm::Print(":");
        EarlyTerm::PrintDec(line);
        EarlyTerm::Print("\n------------------------------\n");
        EarlyTerm::Print("System halted. Restart required.\n");
        
        // Output to serial for remote debugging
        WriteToSerial(message);
        
        // Halt forever
        while (true) {
            Platform::Halt();
        }
    }
    
    void PanicWithContext(const char* message, void* cpu_context) {
        Platform::DisableInterrupts();
        WriteToScreen(message);
        DumpRegisters(cpu_context);
        WriteToSerial(message);
        while (true) Platform::Halt();
    }
    
    void DumpRegisters(void* context) {
        // TODO: Parse x86_64 register context and print
        EarlyTerm::Print("[Registers not available]\n");
    }
    
    bool SavePanicLog() {
        // Would save to VFS - risky during panic
        return false;
    }
}
