#include "verbose.h"
#include "early_term.h"
#include "graphics.h"

namespace Verbose {
    static bool enabled = true;
    
    // Color definitions (BGRA)
    static const uint32_t COL_OK    = 0xFF00FF00;  // Green
    static const uint32_t COL_WARN  = 0xFF00FFFF;  // Yellow
    static const uint32_t COL_ERR   = 0xFF0000FF;  // Red
    static const uint32_t COL_INFO  = 0xFFFFFF00;  // Cyan
    static const uint32_t COL_DEBUG = 0xFF808080;  // Gray
    static const uint32_t COL_WHITE = 0xFFFFFFFF;
    
    void Init() {
        enabled = true;
    }
    
    void SetEnabled(bool e) { enabled = e; }
    bool IsEnabled() { return enabled; }
    
    void Log(LogLevel level, const char* tag, const char* message) {
        if (!enabled) return;
        
        uint32_t color;
        const char* prefix;
        
        switch (level) {
            case LogLevel::OK:
                color = COL_OK;
                prefix = "[OK] ";
                break;
            case LogLevel::WARN:
                color = COL_WARN;
                prefix = "[WARN] ";
                break;
            case LogLevel::ERR:
                color = COL_ERR;
                prefix = "[ERR] ";
                break;
            case LogLevel::INFO:
                color = COL_INFO;
                prefix = "[INFO] ";
                break;
            case LogLevel::DEBUG:
                color = COL_DEBUG;
                prefix = "[DBG] ";
                break;
            default:
                color = COL_WHITE;
                prefix = "";
        }
        
        // Save current color
        uint32_t oldFG = EarlyTerm::colorFG;
        
        // Print prefix in color
        EarlyTerm::colorFG = color;
        EarlyTerm::Print(prefix);
        
        // Print tag in white
        EarlyTerm::colorFG = COL_WHITE;
        EarlyTerm::Print(tag);
        EarlyTerm::Print(": ");
        
        // Print message
        EarlyTerm::Print(message);
        EarlyTerm::Print("\n");
        
        // Restore color
        EarlyTerm::colorFG = oldFG;
    }
    
    void OK(const char* tag, const char* msg) {
        Log(LogLevel::OK, tag, msg);
    }
    
    void Warn(const char* tag, const char* msg) {
        Log(LogLevel::WARN, tag, msg);
    }
    
    void Error(const char* tag, const char* msg) {
        Log(LogLevel::ERR, tag, msg);
    }
    
    void Info(const char* tag, const char* msg) {
        Log(LogLevel::INFO, tag, msg);
    }
    
    void Debug(const char* tag, const char* msg) {
        Log(LogLevel::DEBUG, tag, msg);
    }
    
    void PrintColor(uint32_t color, const char* text) {
        uint32_t oldFG = EarlyTerm::colorFG;
        EarlyTerm::colorFG = color;
        EarlyTerm::Print(text);
        EarlyTerm::colorFG = oldFG;
    }
    
    void PrintHexColor(uint32_t color, uint64_t value) {
        uint32_t oldFG = EarlyTerm::colorFG;
        EarlyTerm::colorFG = color;
        EarlyTerm::PrintHex(value);
        EarlyTerm::colorFG = oldFG;
    }   
}
