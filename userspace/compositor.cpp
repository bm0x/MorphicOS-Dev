#include "compositor.h"

// Static member storage
uint32_t* Compositor::frontBuffer = nullptr;
uint32_t* Compositor::backBuffer = nullptr;
int Compositor::width = 0;
int Compositor::height = 0;
int Compositor::bpp = 32; // Assuming 32

bool Compositor::Initialize() {
    // 1. Get Screen Geometry
    uint64_t info = sys_get_screen_info();
    width = (info >> 32) & 0xFFFFFFFF;
    height = info & 0xFFFFFFFF;
    
    if (width == 0 || height == 0) return false;
    
    // 2. Map Front Buffer (VRAM)
    frontBuffer = (uint32_t*)sys_video_map();
    if (!frontBuffer) return false;
    
    // 3. Allocate Back Buffer (RAM)
    // Size = Width * Height * 4 bytes
    uint64_t size = (uint64_t)width * height * 4;
    uint64_t backAddr = sys_alloc_backbuffer(size);
    
    if (backAddr == 0) return false;
    
    backBuffer = (uint32_t*)backAddr;
    
    return true;
}

void Compositor::Clear(uint32_t color) {
    if (!backBuffer) return;
    
    // Optimized fill (naive loop for now, good enough for 800x600 in O2)
    uint64_t total_pixels = (uint64_t)width * height;
    for (uint64_t i = 0; i < total_pixels; i++) {
        backBuffer[i] = color;
    }
}

void Compositor::DrawRect(int x, int y, int w, int h, uint32_t color) {
    if (!backBuffer) return;
    
    // Clipping
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;
    
    for (int j = 0; j < h; j++) {
        uint32_t* row = &backBuffer[(y + j) * width + x];
        for (int i = 0; i < w; i++) {
            row[i] = color;
        }
    }
}

// Simple Arrow Bitmap (12x18) code: 0=Trans, 1=White, 2=Black
static const uint8_t cursor_w = 12;
static const uint8_t cursor_h = 18;
static const uint8_t cursor_bitmap[] = {
    2,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,
    2,1,1,1,1,1,2,2,2,2,2,0,
    2,1,1,2,1,1,2,0,0,0,0,0,
    2,1,2,0,2,1,1,2,0,0,0,0,
    2,2,0,0,2,1,1,2,0,0,0,0,
    2,0,0,0,0,2,1,1,2,0,0,0,
    0,0,0,0,0,2,1,1,2,0,0,0,
    0,0,0,0,0,0,2,2,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0
};

void Compositor::DrawCursor(int x, int y) {
    for (int cy = 0; cy < cursor_h; cy++) {
        for (int cx = 0; cx < cursor_w; cx++) {
            int px = x + cx;
            int py = y + cy;
            
            if (px < 0 || px >= width || py < 0 || py >= height) continue;
            
            uint8_t code = cursor_bitmap[cy * cursor_w + cx];
            uint32_t color = 0;
            
            if (code == 1) color = 0xFFFFFFFF; // White
            else if (code == 2) color = 0xFF000000; // Black
            else continue; // Transparent behavior
            
            backBuffer[py * width + px] = color;
        }
    }
}

void Compositor::SwapBuffers() {
    if (!backBuffer || !frontBuffer) return;
    
    // Copy Back -> Front
    // 4MB copy per frame @ 60Hz = 240MB/s bandwidth. Trivial for modern RAM.
    uint64_t total_pixels = (uint64_t)width * height;
    
    // Unrolled copy or simple copy?
    // Let's rely on compiler loop optimization for now.
    // If flickering persists due to copy speed ("tearing"), we need SSE.
    // But tearing is usually due to lack of VSYNC, not copy speed.
    // Copy speed affects FPS cap.
    
    for (uint64_t i = 0; i < total_pixels; i++) {
        frontBuffer[i] = backBuffer[i];
    }
}

void Compositor::RenderScene(Window* windows, int windowCount, int mouseX, int mouseY) {
    // 1. Clear Background (Wallpaper or Scale solid color)
    Clear(0xFF202020); // Dark Grey
    
    // 2. Draw Taskbar (Bottom layer UI)
    DrawRect(0, height - 40, width, 40, 0xFF101010);
    // Taskbar Start Button
    DrawRect(10, height - 35, 60, 30, 0xFF404040); 
    
    // 3. Draw Windows (Middle layer)
    for (int i = 0; i < windowCount; i++) {
        Window& w = windows[i];
        // Body
        DrawRect(w.x, w.y, w.width, w.height, w.color);
        // Title Bar (simple contrast)
        DrawRect(w.x, w.y, w.width, 25, 0xFFCCCCCC);
    }
    
    // 4. Draw Cursor (Top Layer)
    DrawCursor(mouseX, mouseY);
}
