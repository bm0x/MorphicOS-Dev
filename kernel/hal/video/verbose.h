#pragma once

#include <stdint.h>

// Log levels with colors
enum class LogLevel {
    OK,      // Green
    WARN,    // Yellow
    ERR,     // Red
    INFO,    // Cyan
    DEBUG    // Gray
};

// Verbose Engine - Advanced color logging
namespace Verbose {
    // Initialize verbose system
    void Init();
    
    // Log with level
    void Log(LogLevel level, const char* tag, const char* message);
    
    // Shorthand macros
    void OK(const char* tag, const char* msg);
    void Warn(const char* tag, const char* msg);
    void Error(const char* tag, const char* msg);
    void Info(const char* tag, const char* msg);
    void Debug(const char* tag, const char* msg);
    
    // Print with specific color
    void PrintColor(uint32_t color, const char* text);
    
    // Hex value in color
    void PrintHexColor(uint32_t color, uint64_t value);
    
    // Enable/disable verbose mode
    void SetEnabled(bool enabled);
    bool IsEnabled();
}
