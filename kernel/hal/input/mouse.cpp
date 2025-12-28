#include "mouse.h"
#include "../arch/x86_64/io.h"
#include "../video/graphics.h"
#include "../video/early_term.h"
#include "../../utils/std.h"

// SIMD blit functions
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
}

namespace Mouse {
    // Position and button state
    static int16_t posX = 0;
    static int16_t posY = 0;
    static uint8_t buttons = 0;
    static uint16_t maxX = 1024;
    static uint16_t maxY = 768;
    static bool visible = true;
    
    // PS/2 Mouse state machine
    static uint8_t cycle = 0;
    static uint8_t packet[3];
    
    // === ZERO-LATENCY OVERLAY SYSTEM ===
    static bool fastPathEnabled = false;
    static uint32_t* backbufferPtr = nullptr;
    static uint32_t* framebufferPtr = nullptr;
    static uint32_t bufferPitch = 0;
    
    // Background restoration buffer (32x32 max cursor)
    static uint32_t restoreBuffer[CURSOR_BUFFER_SIZE];
    static int16_t lastX = -1;
    static int16_t lastY = -1;
    static bool hasStoredBackground = false;
    static bool cursorDrawnThisFrame = false;
    
    // === CURSOR STATE MACHINE ===
    static CursorVisibility cursorVisibility = CursorVisibility::HIDDEN;
    static VisualContext currentContext = VisualContext::TEXT_SHELL;

    
    // Pre-rendered cursor sprite (16x16 white arrow with black outline)
    static const uint32_t cursorSprite[CURSOR_BUFFER_SIZE] = {
        0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    
    void Init() {
        // Enable PS/2 mouse (auxiliary device)
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xA8);
        
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0x20);
        while (!(IO::inb(0x64) & 1));
        uint8_t status = IO::inb(0x60);
        status |= 2;  // Enable IRQ12
        
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0x60);
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, status);
        
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xD4);
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, 0xF6);
        while (!(IO::inb(0x64) & 1));
        IO::inb(0x60);
        
        while (IO::inb(0x64) & 2);
        IO::outb(0x64, 0xD4);
        while (IO::inb(0x64) & 2);
        IO::outb(0x60, 0xF4);
        while (!(IO::inb(0x64) & 1));
        IO::inb(0x60);
        
        posX = maxX / 2;
        posY = maxY / 2;
        
        EarlyTerm::Print("[Mouse] PS/2 initialized.\n");
    }
    
    void InitOverlay(uint32_t* backbuffer, uint32_t pitch) {
        backbufferPtr = backbuffer;
        bufferPitch = pitch;
        hasStoredBackground = false;
        lastX = -1;
        lastY = -1;
    }
    
    void EnableFastPath(bool enable) {
        fastPathEnabled = enable;
    }
    
    void SaveBackground(int16_t x, int16_t y) {
        if (!backbufferPtr || x < 0 || y < 0) return;
        
        for (int row = 0; row < CURSOR_HEIGHT && (y + row) < maxY; row++) {
            for (int col = 0; col < CURSOR_WIDTH && (x + col) < maxX; col++) {
                restoreBuffer[row * CURSOR_WIDTH + col] = 
                    backbufferPtr[(y + row) * bufferPitch + (x + col)];
            }
        }
        lastX = x;
        lastY = y;
        hasStoredBackground = true;
    }
    
    void RestoreBackground() {
        if (!backbufferPtr || !hasStoredBackground || lastX < 0 || lastY < 0) return;
        
        for (int row = 0; row < CURSOR_HEIGHT && (lastY + row) < maxY; row++) {
            for (int col = 0; col < CURSOR_WIDTH && (lastX + col) < maxX; col++) {
                backbufferPtr[(lastY + row) * bufferPitch + (lastX + col)] = 
                    restoreBuffer[row * CURSOR_WIDTH + col];
            }
        }
    }
    
    void DrawCursorFast() {
        if (!backbufferPtr || !visible) return;
        
        for (int row = 0; row < CURSOR_HEIGHT && (posY + row) < maxY; row++) {
            for (int col = 0; col < CURSOR_WIDTH && (posX + col) < maxX; col++) {
                uint32_t pixel = cursorSprite[row * CURSOR_WIDTH + col];
                if (pixel != 0x00000000) {  // Not transparent
                    backbufferPtr[(posY + row) * bufferPitch + (posX + col)] = pixel;
                }
            }
        }
    }
    
    void OnInterrupt() {
        uint8_t status = IO::inb(0x64);
        if (!(status & 0x20)) return;
        
        uint8_t data = IO::inb(0x60);
        packet[cycle] = data;
        cycle = (cycle + 1) % 3;
        
        if (cycle == 0) {
            if (!(packet[0] & 0x08)) return;
            
            buttons = packet[0] & 0x07;
            
            int16_t dx = packet[1];
            int16_t dy = packet[2];
            
            if (packet[0] & 0x10) dx |= 0xFF00;
            if (packet[0] & 0x20) dy |= 0xFF00;
            
            int16_t oldX = posX;
            int16_t oldY = posY;
            
            posX += dx;
            posY -= dy;
            
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;
            if (posX >= maxX) posX = maxX - 1;
            if (posY >= maxY) posY = maxY - 1;
            
            // === FAST PATH: Immediate cursor update ===
            if (fastPathEnabled && backbufferPtr && (posX != oldX || posY != oldY)) {
                RestoreBackground();
                SaveBackground(posX, posY);
                DrawCursorFast();
                // Direct flip of cursor region to framebuffer
                Graphics::FlipRect(oldX, oldY, CURSOR_WIDTH, CURSOR_HEIGHT);
                Graphics::FlipRect(posX, posY, CURSOR_WIDTH, CURSOR_HEIGHT);
            }
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
    
    // === CURSOR STATE MACHINE ===
    
    void SetVisibility(CursorVisibility state) {
        if (cursorVisibility == state) return;
        
        // If transitioning from visible to hidden, restore background
        if (cursorVisibility == CursorVisibility::VISIBLE_GUI && state == CursorVisibility::HIDDEN) {
            if (hasStoredBackground && backbufferPtr) {
                RestoreBackground();
                Graphics::FlipRect(lastX, lastY, CURSOR_WIDTH, CURSOR_HEIGHT);
            }
        }
        
        cursorVisibility = state;
        visible = (state == CursorVisibility::VISIBLE_GUI);
    }
    
    CursorVisibility GetVisibility() {
        return cursorVisibility;
    }
    
    void RefreshCursor() {
        // Only refresh in GUI mode with fast path
        if (cursorVisibility != CursorVisibility::VISIBLE_GUI) return;
        if (!fastPathEnabled || !backbufferPtr) return;
        
        // ALWAYS redraw cursor after MorphicGUI::Draw() repainted the frame
        // This ensures cursor stays visible even when static
        SaveBackground(posX, posY);
        DrawCursorFast();
        Graphics::FlipRect(posX, posY, CURSOR_WIDTH, CURSOR_HEIGHT);
    }

    
    void SetVisualContext(VisualContext mode) {
        if (currentContext == mode) return;
        
        VisualContext oldContext = currentContext;
        currentContext = mode;
        
        if (mode == VisualContext::GRAPHICAL_GUI) {
            // Entering desktop mode
            SetVisibility(CursorVisibility::VISIBLE_GUI);
            EnableFastPath(true);
        } else {
            // Exiting to text shell
            SetVisibility(CursorVisibility::HIDDEN);
            EnableFastPath(false);
            
            // Clear any cursor artifacts
            if (hasStoredBackground && backbufferPtr) {
                RestoreBackground();
            }
            hasStoredBackground = false;
            lastX = -1;
            lastY = -1;
            
            // Full screen clear handled by caller
        }
    }
    
    VisualContext GetVisualContext() {
        return currentContext;
    }
    
    void DrawCursor() {
        if (!visible) return;
        for (int y = 0; y < CURSOR_HEIGHT; y++) {
            for (int x = 0; x < CURSOR_WIDTH; x++) {
                uint32_t pixel = cursorSprite[y * CURSOR_WIDTH + x];
                if (pixel != 0x00000000) {
                    Graphics::PutPixel(posX + x, posY + y, pixel);
                }
            }
        }
    }
    
    void HideCursor() {
        if (fastPathEnabled && hasStoredBackground) {
            RestoreBackground();
        }
    }
    
    void SetCursorVisible(bool v) { visible = v; }
    
    bool PollEvent(MouseEvent* event) {
        return false;
    }
}
