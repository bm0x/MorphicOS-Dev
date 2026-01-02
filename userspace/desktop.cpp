#include <stdint.h>
#include <stddef.h>

// === MOCK SYSCALLS (In a real scenario, these would be in a header) ===
// These stubs simulate the kernel API interaction
extern "C" {
    void* sys_video_map();       // Returns pointer to backbuffer/framebuffer
    void  sys_video_flip();      // Swaps buffers (V-Sync)
    int   sys_input_poll(void* event); // Polls input events
    void  sys_sleep(uint32_t ms);
}

// Simple color definitions
#define COLOR_BG      0xFF202020
#define COLOR_TASKBAR 0xFF101010
#define COLOR_WHITE   0xFFFFFFFF

// Globals
uint32_t* video_memory = nullptr;
const int SCREEN_WIDTH = 1024; // Fixed for now, normally obtained via syscall
const int SCREEN_HEIGHT = 768;

void DrawRect(int x, int y, int w, int h, uint32_t color) {
    if (!video_memory) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            video_memory[(y + j) * SCREEN_WIDTH + (x + i)] = color;
        }
    }
}

void DrawTaskbar() {
    // Draw bottom bar
    DrawRect(0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, 40, COLOR_TASKBAR);
    // Start Button (Mock)
    DrawRect(10, SCREEN_HEIGHT - 35, 60, 30, 0xFF404040);
}

void DrawWallpaper() {
    // Fill screen with dark grey
    // Direct memory access memset simulation (loop)
    // Faster than syscall 'DrawRect' in typical microkernel if mapped properly
    uint32_t size = SCREEN_WIDTH * SCREEN_HEIGHT;
    for (uint32_t i = 0; i < size; i++) {
        video_memory[i] = COLOR_BG;
    }
}

// Entry Point
// The Loader passes the pointer to the Assets segment of the MPK
int main(void* asset_ptr) {
    // 1. Get Direct Video Access
    video_memory = (uint32_t*)sys_video_map();
    
    if (!video_memory) {
        return -1; // Panic
    }
    
    // Use asset_ptr to load wallpaper.raw if available
    // (Skeleton implementation)

    
    // 2. Main Desktop Loop
    while (1) {
        // A. Draw Background (Optimization: distinct loops for wallpaper vs UI?)
        DrawWallpaper();
        
        // B. Draw UI Elements
        DrawTaskbar();
        
        // C. FLIP (V-Sync)
        // This tells the kernel "I am done writing, show this frame"
        sys_video_flip();
        
        // D. Input (Non-blocking)
        // sys_input_poll(&event);
    }
    
    return 0;
}
