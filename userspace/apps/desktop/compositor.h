#pragma once
#include <stdint.h>
#include <stddef.h>

// Forward declarations of syscalls we need
extern "C" {
    void* sys_video_map();
    uint64_t sys_get_screen_info();
    uint64_t sys_alloc_backbuffer(uint64_t size);
    // arg1: backbuffer pointer (tightly packed width*height BGRA32)
    // returns: 1 if VSync wait succeeded (best-effort), 0 otherwise
    uint64_t sys_video_flip(void* backbuffer);
}

class Compositor {
public:
    static bool Initialize();
    static void Clear(uint32_t color);
    // Presents the backbuffer to the framebuffer.
    // Returns true if the kernel reports a VSync-aligned present (best-effort).
    static bool SwapBuffers();
    
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
    };
    
    // Renders the entire scene: Background -> Windows -> Mouse
    static void RenderScene(Window* windows, int windowCount, int mouseX, int mouseY);
    
private:
    static uint32_t* frontBuffer; // VRAM
    static uint32_t* backBuffer;  // RAM
    static int width;
    static int height;
    static int bpp; // Assumed 32
};
