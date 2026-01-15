#pragma once
#include <stdint.h>
#include <stddef.h>
#include "system_info.h"

// Forward declarations of syscalls we need
extern "C" {
    void* sys_video_map();
    uint64_t sys_get_screen_info();
    uint64_t sys_alloc_backbuffer(uint64_t size);
    // arg1: backbuffer pointer (tightly packed width*height BGRA32)
    // returns: 1 if VSync wait succeeded (best-effort), 0 otherwise
    uint64_t sys_video_flip(void* backbuffer);

    // arg1: backbuffer pointer
    // arg2: (x<<32) | y
    // arg3: (w<<32) | h
    uint64_t sys_video_flip_rect(void* backbuffer, uint64_t xy, uint64_t wh);
}

class Compositor {
public:
    static bool Initialize();
    static void Clear(uint32_t color);
    // Presents the backbuffer to the framebuffer.
    // Returns true if the kernel reports a VSync-aligned present (best-effort).
    static bool SwapBuffers();

    // Presents only a dirty rectangle (major perf win).
    static bool SwapBuffersRect(int x, int y, int w, int h);
    
    // Drawing Primitives
    static void DrawRect(int x, int y, int w, int h, uint32_t color);
    static void DrawCursor(int x, int y);
    
    // Getters
    static int GetWidth() { return width; }
    static int GetHeight() { return height; }
    
    // Window Management specific
    struct Window {
        int x, y, width, height;
        uint32_t color;
        const char* title;
        bool is_dragging;
        bool minimized;
        bool maximized;
        int restore_x, restore_y, restore_w, restore_h;
    };
    
    // Renders the entire scene: Background -> Windows -> Mouse
    static void RenderScene(Window* windows, int windowCount, int mouseX, int mouseY);

    // Minimal UI helpers
    static void RenderTaskbar(Window* windows, int windowCount, 
                              void* extWindows, int extWindowCount,
                              bool menuOpen, const MorphicDateTime& dt);
    static void RenderMenu(bool menuOpen);

    // Minimal text rendering (tiny pixel font)
    static void DrawText(int x, int y, const char* text, uint32_t color, int scale = 1);

    // Partial redraw clip control
    static void SetClip(int x, int y, int w, int h);
    static void ClearClip();
    
private:
    static uint32_t* frontBuffer; // VRAM
    static uint32_t* backBuffer;  // RAM
    static int width;
    static int height;
    static int bpp; // Assumed 32

    // Simple clip rect for partial redraw
    static bool clipEnabled;
    static int clipX, clipY, clipW, clipH;
    static bool IntersectsClip(int x, int y, int w, int h);
    static void FillRectClipped(int x, int y, int w, int h, uint32_t color);
    static void DrawRectClipped(int x, int y, int w, int h, uint32_t color);
    static void DrawCursorClipped(int x, int y);
    static void DrawSevenSegDigit(int x, int y, int scale, int digit, uint32_t onColor, uint32_t offColor);
    static void DrawClockHHMM(int rightX, int y, int scale, uint32_t timeSeconds);
    static void DrawClockText(int rightX, int y, uint32_t hh, uint32_t mm, uint32_t ss, uint32_t dd, uint32_t mo);
    static void DrawChar(int x, int y, char ch, uint32_t color, int scale);
};
