#include "os_event.h"
#include "compositor.h"

// Syscall stubs not covered by compositor.h
extern "C" {
    uint64_t sys_get_time_ms(); 
    void  sys_sleep(uint32_t ms);
    int   sys_get_event(OSEvent* ev); // Returns 1 if event, 0 if none
}

// ... colors ...

// Globals for Mouse
int mouse_x = 400;
int mouse_y = 300;
// Note: DX/DY are now event-driven, not bounce simulated

// Globals for Windows
#define MAX_WINDOWS 5
Compositor::Window windows[MAX_WINDOWS];
int window_count = 0;

// Interaction State
int drag_target_idx = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;

void InitWindows() {
    windows[0] = {100, 100, 400, 300, 0xFF4040A0, "File Manager", false};
    windows[1] = {550, 150, 300, 200, 0xFF40A040, "Terminal", false};
    window_count = 2;
}

void HandleEvent(const OSEvent& ev) {
    if (ev.type == OSEvent::MOUSE_MOVE) {
        // [ENGINEER-FIX] Userspace Coordinate Update
        // ev.dx/dy come signed from kernel.
        mouse_x += ev.dx;
        mouse_y += ev.dy;
        
        // [ENGINEER-FIX] Clamping Logic
        // Prevents cursor from escaping the framebuffer
        int w = Compositor::GetWidth();
        int h = Compositor::GetHeight();
        
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= w) mouse_x = w - 1;
        if (mouse_y >= h) mouse_y = h - 1;
        
        // Handle Dragging
        if (drag_target_idx != -1) {
            windows[drag_target_idx].x = mouse_x - drag_offset_x;
            windows[drag_target_idx].y = mouse_y - drag_offset_y;
        }
    }
    else if (ev.type == OSEvent::MOUSE_MOVE) { // Mouse buttons are often in MOVE packet too, check buttons
        // Actually OSEvent usually has separate CLICK type or buttons state in MOVE?
        // My shared/os_event.h defines MOUSE_MOVE=1, MOUSE_CLICK=2
        // Let's assume buttons are updated in MOVE too or specialized events.
        // For now, let's check buttons state if provided in MOVE or separate CLICK event.
    }
    
    // Simple Button Logic (Assuming we receive button state in every packet or separate events)
    // Let's make it robust: Check ev.buttons if provided.
    // NOTE: My kernel mouse driver sends MOUSE_MOVE with buttons state.
    
    if (ev.type == OSEvent::MOUSE_MOVE) {
         bool left_down = (ev.buttons & 1); // Bit 0 = Left
         
         if (left_down) {
             if (drag_target_idx == -1) {
                 // Try to start drag (Hit Test)
                 // Reverse order to pick top-most window
                 for (int i = window_count - 1; i >= 0; i--) {
                     Compositor::Window& w = windows[i];
                     // Hit Title Bar? (Height ~25)
                     if (mouse_x >= w.x && mouse_x < w.x + w.width &&
                         mouse_y >= w.y && mouse_y < w.y + 25) {
                         
                         drag_target_idx = i;
                         drag_offset_x = mouse_x - w.x;
                         drag_offset_y = mouse_y - w.y;
                         
                         // Move to top (simple swap to end of list)
                         if (i != window_count - 1) {
                             Compositor::Window temp = windows[window_count - 1];
                             windows[window_count - 1] = w;
                             windows[i] = temp;
                             drag_target_idx = window_count - 1;
                         }
                         break;
                     }
                 }
             }
         } else {
             // Button up
             drag_target_idx = -1;
         }
    }
}

void PollInput() {
    OSEvent ev;
    // Process up to 10 events per frame to avoid lag
    int count = 0;
    while (sys_get_event(&ev) && count < 10) {
        HandleEvent(ev);
        count++;
    }
}

extern "C" int main(void* asset_ptr) {
    if (!Compositor::Initialize()) return -1;
    
    InitWindows();
    
    const int TARGET_FPS = 60;
    const int FRAME_TIME_MS = 1000 / TARGET_FPS;
    
    while (1) {
        uint64_t start_time = sys_get_time_ms();
        
        // A. Input
        PollInput();
        
        // B. Update Logic (Physics removed, using Mouse input now)
        // ...
        
        // C. Render
        Compositor::RenderScene(windows, window_count, mouse_x, mouse_y);
        
        // D. Swap
        Compositor::SwapBuffers();
        
        // E. Sync
        uint64_t end_time = sys_get_time_ms();
        uint64_t elapsed = end_time - start_time;
        if (elapsed < FRAME_TIME_MS) {
            sys_sleep(FRAME_TIME_MS - elapsed);
        }
    }
    return 0;
}
