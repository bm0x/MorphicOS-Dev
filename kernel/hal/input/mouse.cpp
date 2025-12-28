#include "mouse.h"
#include "../arch/x86_64/io.h"
#include "../video/graphics.h"
#include "../video/early_term.h"
#include "../../utils/std.h"

namespace Mouse {
    static int16_t posX = 0;
    static int16_t posY = 0;
    static uint8_t buttons = 0;
    static uint16_t maxX = 640;
    static uint16_t maxY = 480;
    static bool visible = true;
    
    // PS/2 Mouse state machine
    static uint8_t cycle = 0;
    static uint8_t packet[3];
    
    // Simple arrow cursor (8x8)
    static const uint8_t cursorData[8] = {
        0b10000000,
        0b11000000,
        0b11100000,
        0b11110000,
        0b11111000,
        0b11100000,
        0b10100000,
        0b00100000
    };
    
    void Init() {
        // Enable PS/2 mouse (auxiliary device)
        // Wait for controller ready
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xA8);  // Enable auxiliary device
        
        // Enable interrupts for mouse
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0x20);  // Get compaq status
        while (!(IO::inb(0x64) & 1));
        uint8_t status = IO::inb(0x60);
        status |= 2;  // Enable IRQ12
        
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0x60);  // Set compaq status
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, status);
        
        // Tell mouse to use default settings
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xD4);  // Write to mouse
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, 0xF6);  // Set defaults
        while (!(IO::inb(0x64) & 1));
        IO::inb(0x60);  // ACK
        
        // Enable data reporting
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xD4);
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, 0xF4);  // Enable
        while (!(IO::inb(0x64) & 1));
        IO::inb(0x60);  // ACK
        
        posX = maxX / 2;
        posY = maxY / 2;
        
        EarlyTerm::Print("[Mouse] PS/2 initialized.\n");
    }
    
    void OnInterrupt() {
        uint8_t status = IO::inb(0x64);
        if (!(status & 0x20)) return;  // Not mouse data
        
        uint8_t data = IO::inb(0x60);
        
        packet[cycle] = data;
        cycle = (cycle + 1) % 3;
        
        if (cycle == 0) {
            // Complete packet
            if (!(packet[0] & 0x08)) return;  // Invalid packet
            
            buttons = packet[0] & 0x07;
            
            int16_t dx = packet[1];
            int16_t dy = packet[2];
            
            if (packet[0] & 0x10) dx |= 0xFF00;  // Sign extend X
            if (packet[0] & 0x20) dy |= 0xFF00;  // Sign extend Y
            
            posX += dx;
            posY -= dy;  // Y is inverted
            
            // Clamp to bounds
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;
            if (posX >= maxX) posX = maxX - 1;
            if (posY >= maxY) posY = maxY - 1;
        }
    }
    
    int16_t GetX() { return posX; }
    int16_t GetY() { return posY; }
    uint8_t GetButtons() { return buttons; }
    bool IsLeftPressed() { return buttons & MOUSE_LEFT; }
    bool IsRightPressed() { return buttons & MOUSE_RIGHT; }
    
    void SetBounds(uint16_t w, uint16_t h) {
        maxX = w;
        maxY = h;
        if (posX >= maxX) posX = maxX - 1;
        if (posY >= maxY) posY = maxY - 1;
    }
    
    void DrawCursor() {
        if (!visible) return;
        
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (cursorData[y] & (0x80 >> x)) {
                    Graphics::PutPixel(posX + x, posY + y, COLOR_WHITE);
                }
            }
        }
    }
    
    void HideCursor() {
        // With double buffering, just don't draw cursor before flip
    }
    
    void SetCursorVisible(bool v) { visible = v; }
    
    bool PollEvent(MouseEvent* event) {
        // For now, events are handled via interrupts
        // This could be extended for event queue
        return false;
    }
}
