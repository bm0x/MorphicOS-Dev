#include "mouse.h"
#include "input_device.h"
#include "input_event.h"
#include "evdev.h"
#include "../arch/x86_64/io.h"
#include "../video/graphics.h"
#include "../video/compositor.h"
#include "../video/early_term.h"
#include "../serial/uart.h"
#include "../../utils/std.h"
#include "focus_manager.h"

// SIMD blit functions
extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
}

namespace Mouse {
    // Position and button state
    static int16_t posX = 0;
    static int16_t posY = 0;
    static uint8_t buttons = 0;
    static uint8_t lastButtons = 0;
    static uint16_t maxX = 1024;
    static uint16_t maxY = 768;
    static bool visible = true;
    
    // Simple Arrow Bitmap (12x18) code: 0=Trans, 1=White, 2=Black
    static const uint8_t cursor_w = 12;
    static const uint8_t cursor_h = 18;
    static const uint8_t cursor_bitmap[] = {
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0,
        2, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0,
        2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 0, 2, 1, 1, 2, 1, 1, 2, 0, 0, 0, 0, 0,
        2, 1, 2, 0, 2, 1, 1, 2, 0, 0, 0, 0, 2, 2, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0,
        2, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    
    // PS/2 Mouse state machine
    static uint8_t cycle = 0;
    static uint8_t packet[3];

    // Optional low-rate debug (prints to serial only)
    // Enable by adding -DMOUSE_DEBUG to CXXFLAGS.
    static uint64_t irq12Samples = 0;
    static uint64_t irq12IrqCount = 0;

    static inline bool PS2_WaitInputClear(uint32_t spins = 100000) {
        while (spins--) {
            if ((IO::inb(0x64) & 0x02) == 0) return true; // input buffer empty
            __asm__ volatile("pause");
        }
        return false;
    }

    static inline bool PS2_WaitOutputFull(uint32_t spins = 100000) {
        while (spins--) {
            if (IO::inb(0x64) & 0x01) return true; // output buffer full
            __asm__ volatile("pause");
        }
        return false;
    }

    static inline void PS2_FlushOutput() {
        // Drain any pending bytes to avoid desync after init/UEFI leftovers
        for (int i = 0; i < 64; i++) {
            uint8_t st = IO::inb(0x64);
            if (!(st & 0x01)) break;
            (void)IO::inb(0x60);
        }
    }

    static inline void PS2_WriteController(uint8_t cmd) {
        if (!PS2_WaitInputClear()) return;
        IO::outb(0x64, cmd);
    }

    static inline void PS2_WriteData(uint8_t data) {
        if (!PS2_WaitInputClear()) return;
        IO::outb(0x60, data);
    }

    static inline void PS2_MouseWrite(uint8_t data) {
        // Prefix 0xD4 routes next byte to the aux device
        PS2_WriteController(0xD4);
        PS2_WriteData(data);
    }

    static inline uint8_t PS2_ReadData() {
        if (!PS2_WaitOutputFull()) return 0;
        return IO::inb(0x60);
    }
    
    // === ZERO-LATENCY OVERLAY SYSTEM REMOVED ===
    // Cleanup: Variables removed as part of Phase 5 Refactor

    
    // === CURSOR STATE MACHINE ===
    static CursorVisibility cursorVisibility = CursorVisibility::HIDDEN;
    static VisualContext currentContext = VisualContext::TEXT_SHELL;

    
    // Pre-rendered cursor sprite (16x16 white arrow with black outline)
    // Cursor Sprite Removed (Now drawn by Userspace)
    
    void Init() {
        // Robust PS/2 init: flush buffers, enable aux device, enable IRQ12 in controller,
        // set defaults and enable streaming.
        PS2_FlushOutput();

        // Enable auxiliary device (mouse)
        PS2_WriteController(0xA8);

        // Read controller command byte
        PS2_WriteController(0x20);
        uint8_t cmdByte = PS2_ReadData();

        // Enable IRQ12 (bit 1). Keep other bits as-is.
        cmdByte |= 0x02;

        // Write controller command byte back
        PS2_WriteController(0x60);
        PS2_WriteData(cmdByte);

        PS2_FlushOutput();

        // Set Defaults (0xF6) and expect ACK (0xFA)
        PS2_MouseWrite(0xF6);
        uint8_t ack1 = PS2_ReadData();

        // Enable Streaming (0xF4) and expect ACK (0xFA)
        PS2_MouseWrite(0xF4);
        uint8_t ack2 = PS2_ReadData();

        // Reset packet state machine
        cycle = 0;

        posX = maxX / 2;
        posY = maxY / 2;

    #ifdef MOUSE_DEBUG
        UART::Write("[Mouse] Init cmdByte="); UART::WriteHex(cmdByte);
        UART::Write(" ack1="); UART::WriteHex(ack1);
        UART::Write(" ack2="); UART::WriteHex(ack2);
        UART::Write("\n");
    #else
        (void)ack1;
        (void)ack2;
    #endif

        EarlyTerm::Print("[Mouse] PS/2 initialized.\n");
    }
    
    // Legacy Overlay Functions Removed (Cleanup)
    void InitOverlay(uint32_t*, uint32_t) {}
    
    void OnInterrupt() {
#ifdef MOUSE_DEBUG
        irq12IrqCount++;
        // Print the first few IRQs, then ~every 4096 IRQs to avoid spamming.
        if (irq12IrqCount <= 3 || ((irq12IrqCount & 0xFFF) == 0)) {
            uint8_t st = IO::inb(0x64);
            UART::Write("[Mouse] IRQ12 enter count="); UART::WriteDec((int64_t)irq12IrqCount);
            UART::Write(" status="); UART::WriteHex(st);
            UART::Write("\n");
        }
#endif
        // ========================================================================
        // [FIX] Robust PS/2 Packet Processing - Linux evdev-inspired
        // Process ONE complete packet per IRQ for synchronization stability.
        // Check AUX bit (0x20) to filter keyboard data from mouse data.
        // ========================================================================
        
        // Maximum bytes to drain per IRQ (prevent infinite loop on bad hardware)
        const int MAX_DRAIN = 16;
        int bytesRead = 0;
        
        while (bytesRead < MAX_DRAIN) {
            uint8_t status = IO::inb(0x64);
            
            // No data available - exit
            if (!(status & 0x01)) {
                return;
            }
            
            // [FIX] Check AUX bit (0x20) - if not set, this is keyboard data, skip it
            // Some emulators don't set this reliably, so we also rely on packet sync
            bool isMouseData = (status & 0x20) != 0;
            
            uint8_t data = IO::inb(0x60);
            bytesRead++;
            
            // If definitely keyboard data, discard and continue
            if (!isMouseData && cycle == 0) {
                // Only skip at cycle 0 to avoid breaking mid-packet
                continue;
            }
            
#ifdef MOUSE_DEBUG
            UART::Write("[Mouse] data=0x"); UART::WriteHex(data); 
            UART::Write(" aux="); UART::WriteDec(isMouseData ? 1 : 0);
            UART::Write(" cycle="); UART::WriteDec(cycle);
            UART::Write("\n");
#endif

            // [FIX] Robust Resync: First byte of packet MUST have bit 3 set
            // If not, this byte is garbage or we're desynchronized
            if (cycle == 0) {
                if (!(data & 0x08)) {
                    // Not a valid packet start - skip this byte entirely
                    // Do NOT advance cycle, keep waiting for valid start byte
                    continue;
                }
            }

            // Store byte in packet buffer
            packet[cycle] = data;
            cycle++;
            
            // [FIX] Process packet only when complete (3 bytes)
            if (cycle < 3) {
                // Packet incomplete - continue reading
                continue;
            }
            
            // Packet complete - reset cycle for next packet
            cycle = 0;

            // [ENGINEER-FIX] 1. Sync Validation
            // Bit 3 of Byte 0 must ALWAYS be 1. If not, we are desynchronized.
            if (!(packet[0] & 0x08)) {
                cycle = 0; // Reset state machine to prevent garbage interpretation
                continue;
            }

            // Optional safety: discard packets with overflow bits set (common PS/2 spec)
            if (packet[0] & 0xC0) {
                cycle = 0;
                continue;
            }

#ifdef MOUSE_DEBUG
            // Log assembled packet bytes
            UART::Write("[Mouse] packet="); UART::WriteHex(packet[0]); UART::Write(" "); UART::WriteHex(packet[1]); UART::Write(" "); UART::WriteHex(packet[2]); UART::Write("\n");
#endif
            
            // [ENGINEER-FIX] 2. Signed Packet Decoding (9-bit)
            // PS/2 Bytes are uint8, but represent parts of a 9-bit signed integer.
            // Byte 0 holds the 9th bit (Sign Bit) for X and Y.
            
            bool x_negative = packet[0] & 0x10;
            bool y_negative = packet[0] & 0x20;
            
            // Start with base 8-bit value (unsigned)
            int16_t rel_x = packet[1];
            int16_t rel_y = packet[2]; // PS/2 Y is positive=Up
            
            // Apply Sign Extension if negative bit is set
            if (x_negative) rel_x |= 0xFF00; // e.g. 0xFF + 0xFF00 = 0xFFFF (-1)
            if (y_negative) rel_y |= 0xFF00;
            
            // Extract Buttons
            buttons = packet[0] & 0x07; // Left=1, Right=2, Middle=4

            // Push click event on button state changes (press/release).
            // This makes userspace dragging reliable even if the press happens without movement.
            if (buttons != lastButtons) {
                OSEvent click;
                click.type = OSEvent::MOUSE_CLICK;
                click.dx = 0;
                click.dy = 0;
                click.buttons = buttons;
                click.scancode = 0;
                InputManager::PushEvent(click);
                
                // [NEW] Also push to evdev subsystem
                // Generate individual button events for each changed button
                uint8_t changed = buttons ^ lastButtons;
                if (changed & 0x01) {  // Left button
                    Evdev::PushButton(Input::BTN_LEFT, (buttons & 0x01) != 0);
                }
                if (changed & 0x02) {  // Right button
                    Evdev::PushButton(Input::BTN_RIGHT, (buttons & 0x02) != 0);
                }
                if (changed & 0x04) {  // Middle button
                    Evdev::PushButton(Input::BTN_MIDDLE, (buttons & 0x04) != 0);
                }
                Evdev::PushSync();
                
                lastButtons = buttons;
            }
            
            // Debug Trace (Critical for verifying hardware response)
            // UART::Write("[MOUSE] dx:"); UART::WriteDec(rel_x); 
            // UART::Write(" dy:"); UART::WriteDec(rel_y);
            // UART::Write("\n");
            
            // Centralized Focus/Cursor Management
            // Forward delta to FocusManager which tracks absolute position and focus
            FocusManager::HandleMouse((int32_t)rel_x, -(int32_t)rel_y, buttons);
            
            // Sync local state for legacy consumers
            auto fState = FocusManager::GetState();
            posX = (int16_t)fState.mouse_x;
            posY = (int16_t)fState.mouse_y;
            
            // Forward to Compositor (Window Manager)
            if (Compositor::ProcessMouseEvent(posX, posY, buttons)) {
                // If handled by Compositor (dragging, buttons), do NOT send to Userspace.
                // This prevents "Ghost Clicks" on the underlying Desktop.
                lastButtons = buttons; // Still update internal state
                return;
            }

            // Push to Userspace
            OSEvent ev;
            ev.type = OSEvent::MOUSE_MOVE;
            ev.dx = (int32_t)rel_x;
            ev.dy = -(int32_t)rel_y; // Invert here for userspace consistency (Up=Negative Delta)
            ev.buttons = buttons;
            ev.scancode = 0;
            
            InputManager::PushEvent(ev);
            
            // [NEW] Also push to evdev subsystem for Linux-style input handling
            // This produces standardized InputEvent structs with timestamps
            Evdev::PushRelativeMotion(rel_x, -rel_y);  // Y inverted for screen coords
            Evdev::PushSync();  // Mark end of event batch

#ifdef MOUSE_DEBUG
            irq12Samples++;
            if ((irq12Samples & 0xFF) == 1) {
                UART::Write("[Mouse] sample dx="); UART::WriteDec(rel_x);
                UART::Write(" dy="); UART::WriteDec(-(int32_t)rel_y);
                UART::Write(" btn="); UART::WriteHex(buttons);
                UART::Write(" x="); UART::WriteDec(posX);
                UART::Write(" y="); UART::WriteDec(posY);
                UART::Write("\n");
            }
#endif
            
            // === ATOMIC UPDATE ===
            // Just update coordinates. Rendering is handled by Atomic Loop (post-flip).
            // No saving/restoring background needed in IRQ.
            
            // Legacy Fast Path block removed to ensure "Golden Path" architecture.
            // (Cursor drawing is now strictly synchronized with V-Sync in widgets.cpp)
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
        cursorVisibility = state;
        visible = (state == CursorVisibility::VISIBLE_GUI);
    }
    
    CursorVisibility GetVisibility() {
        return cursorVisibility;
    }
    
    void RefreshCursor() {}
    
    VisualContext GetVisualContext() {
        return currentContext;
    }

    void SetVisualContext(VisualContext mode) {
        currentContext = mode;
    }
    
    // Legacy / Stubs (Cleanup Phase 5)
    // InitOverlay is defined above (line 144)
    void EnableFastPath(bool) {}
    void SaveBackground(int16_t, int16_t) {}
    void RestoreBackground() {}
    void DrawCursorFast() {}
    bool PollEvent(MouseEvent*) { return false; }
    void RenderCursorAtomic() {}
    void DrawCursor(uint32_t* buffer, uint32_t width, uint32_t height) {
        if (!visible || !buffer) return;
        
        for (int cy = 0; cy < cursor_h; cy++) {
            for (int cx = 0; cx < cursor_w; cx++) {
                int px = posX + cx;
                int py = posY + cy;

                if (px < 0 || px >= (int)width || py < 0 || py >= (int)height)
                    continue;

                uint8_t code = cursor_bitmap[cy * cursor_w + cx];
                uint32_t color = 0;

                if (code == 1)
                    color = 0xFFFFFFFF; // White
                else if (code == 2)
                    color = 0xFF000000; // Black
                else
                    continue; // Transparent

                buffer[py * width + px] = color;
            }
        }
    }
    void HideCursor() {}
    void SetCursorVisible(bool v) { visible = v; }
    
    const uint32_t* GetCursorSprite() { return nullptr; }
}
