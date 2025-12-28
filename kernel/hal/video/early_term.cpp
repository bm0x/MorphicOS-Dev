#include "early_term.h"
#include "font.h"

FramebufferInfo* EarlyTerm::fb = nullptr;
uint32_t EarlyTerm::cursorX = 0;
uint32_t EarlyTerm::cursorY = 0;
uint32_t EarlyTerm::widthChars = 0;
uint32_t EarlyTerm::heightChars = 0;
uint32_t EarlyTerm::colorFG = 0xFFFFFFFF; // White
uint32_t EarlyTerm::colorBG = 0xFF000000; // Black

void EarlyTerm::Init(FramebufferInfo* fbInfo) {
    fb = fbInfo;
    widthChars = fb->width / 8;
    heightChars = fb->height / 16;
    Clear();
}

void EarlyTerm::Clear() {
    if (!fb) return;
    uint32_t* buffer = (uint32_t*)fb->baseAddress;
    uint64_t totalPixels = (uint64_t)fb->width * fb->height;
    for (uint64_t i = 0; i < totalPixels; i++) {
        buffer[i] = colorBG;
    }
    cursorX = 0;
    cursorY = 0;
}

void EarlyTerm::PutPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height) return;
    uint32_t* buffer = (uint32_t*)fb->baseAddress;
    buffer[y * fb->pixelsPerScanLine + x] = color;
}

void EarlyTerm::PutChar(char c) {
    if (!fb) return;

    if (c == '\n') {
        cursorX = 0;
        cursorY++;
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            // Erase character visually
            // We can reuse PutPixel logic or just call PutChar(' ') then move back
            // But PutChar(' ') checks special chars... ' ' is normal.
            
            // Logic: Draw space at new cursorX, cursorY.
            // But we must NOT advance cursor.
            // Easier to manually erase pixels here to avoid recursion/state mess.
            
            uint32_t startX = cursorX * 8;
            uint32_t startY = cursorY * 16;
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 8; x++) {
                    PutPixel(startX + x, startY + y, colorBG);
                }
            }
        }
        // If cursorX == 0, we could wrap back to Y-1... maybe later.
    } else if (c == '\r') {
        cursorX = 0;
    } else if (c == '\t') {
        cursorX = (cursorX + 4) & ~3;
    } else {
        // Draw char
        const uint8_t* glyph = &font8x16[(unsigned char)c * 16];
        uint32_t startX = cursorX * 8;
        uint32_t startY = cursorY * 16;
        
        for (int y = 0; y < 16; y++) {
            uint8_t row = glyph[y];
            for (int x = 0; x < 8; x++) {
                // MSB first
                if ((row >> (7-x)) & 1) {
                    PutPixel(startX + x, startY + y, colorFG);
                } else {
                    PutPixel(startX + x, startY + y, colorBG);
                }
            }
        }
        cursorX++;
    }

    if (cursorX >= widthChars) {
        cursorX = 0;
        cursorY++;
    }

    if (cursorY >= heightChars) {
        Scroll();
        cursorY = heightChars - 1;
    }
}

void EarlyTerm::Scroll() {
    // Simple copy scroll
    uint32_t* buffer = (uint32_t*)fb->baseAddress;
    uint32_t stride = fb->pixelsPerScanLine;
    uint32_t copyHeight = (heightChars - 1) * 16;
    
    // Move lines up
    // Warning: Direct memory access might be slow if uncacheable, 
    // but for "EarlyTerm" it's acceptable.
    // Optimization: Write Combine usually helps here.
    
    // Copy row by row or block? Block is better.
    // buffer size in bytes = stride * 4 * copyHeight
    // We can just loop pixels or use memmove if we had stdlib. 
    // We prefer loop for no dependencies.
    
    for (uint32_t y = 0; y < copyHeight; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            buffer[y * stride + x] = buffer[(y + 16) * stride + x];
        }
    }
    
    // Clear last line
    for (uint32_t y = copyHeight; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            buffer[y * stride + x] = colorBG;
        }
    }
}

void EarlyTerm::Print(const char* str) {
    while (*str) {
        PutChar(*str++);
    }
}

void EarlyTerm::PrintDec(uint64_t value) {
    if (value == 0) {
        PutChar('0');
        return;
    }
    
    char buffer[21];
    int i = 0;
    while (value > 0) {
        buffer[i++] = (value % 10) + '0';
        value /= 10;
    }
    while (i > 0) {
        PutChar(buffer[--i]);
    }
}

void EarlyTerm::PrintHex(uint64_t value) {
    Print("0x");
    char buffer[17];
    const char* digits = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) {
        buffer[15-i] = digits[value & 0xF];
        value >>= 4;
    }
    buffer[16] = 0;
    Print(buffer);
}

void EarlyTerm::SetColor(uint32_t fg, uint32_t bg) {
    colorFG = fg;
    colorBG = bg;
}

void EarlyTerm::PrintAt(uint32_t x, uint32_t y, const char* str) {
    if (!fb) return;
    
    uint32_t currentX = x;
    uint32_t currentY = y;
    
    while (*str) {
        char c = *str++;
        
        // Manual Draw to avoid touching global cursorX/Y
        const uint8_t* glyph = &font8x16[(unsigned char)c * 16];
        uint32_t startX = currentX * 8;
        uint32_t startY = currentY * 16;
        
        for (int py = 0; py < 16; py++) {
            uint8_t row = glyph[py];
            for (int px = 0; px < 8; px++) {
                if ((row >> (7-px)) & 1) {
                    PutPixel(startX + px, startY + py, colorFG);
                } else {
                    PutPixel(startX + px, startY + py, colorBG);
                }
            }
        }
        currentX++;
    }
}
