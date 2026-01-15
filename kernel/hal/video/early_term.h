#ifndef EARLY_TERM_H
#define EARLY_TERM_H

#include "../../../shared/boot_info.h"
#include <stddef.h> 

class EarlyTerm {
public:
    static void Init(FramebufferInfo* fb);
    static void PutChar(char c);
    static void Print(const char* str);
    static void PrintDec(uint64_t value);
    static void PrintHex(uint64_t value);
    static void Clear();
    static void SetColor(uint32_t fg, uint32_t bg);
    static void PrintAt(uint32_t x, uint32_t y, const char* str);
    
    // Disable terminal output (used when Desktop takes over)
    static void Disable();
    static bool IsEnabled();
    // Force re-enable for panic situations (bypasses disabled state)
    static void ForceEnable();
    
    // Public for Verbose Engine color access
    static uint32_t colorFG;
    static uint32_t colorBG;

private:
    static void Scroll();
    static void PutPixel(uint32_t x, uint32_t y, uint32_t color);
    
    static FramebufferInfo* fb;
    static uint32_t cursorX;
    static uint32_t cursorY;
    static uint32_t widthChars;
    static uint32_t heightChars;
    static bool enabled;
};

#endif // EARLY_TERM_H

