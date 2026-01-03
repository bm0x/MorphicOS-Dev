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

static bool left_down = false;

// Smooth mouse movement (fixed-point 16.16, no floats).
static int32_t mouse_x16 = 0;
static int32_t mouse_y16 = 0;
static int32_t mouse_target_x16 = 0;
static int32_t mouse_target_y16 = 0;

static constexpr int MOUSE_SENSITIVITY = 3;

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
    if (ev.type == OSEvent::MOUSE_CLICK) {
        // Kernel sends current button state on transitions.
        left_down = (ev.buttons & 1);
        if (!left_down) {
            drag_target_idx = -1;
        } else if (drag_target_idx == -1) {
            // Start drag immediately on click-down (no movement required).
            const int click_x = (int)(mouse_target_x16 >> 16);
            const int click_y = (int)(mouse_target_y16 >> 16);
            for (int i = window_count - 1; i >= 0; i--) {
                Compositor::Window& w = windows[i];
                if (click_x >= w.x && click_x < w.x + w.width &&
                    click_y >= w.y && click_y < w.y + 25) {
                    drag_target_idx = i;
                    drag_offset_x = click_x - w.x;
                    drag_offset_y = click_y - w.y;

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
        return;
    }

    if (ev.type == OSEvent::MOUSE_MOVE) {
        // If buttons are present in move packets, allow click-to-drag even without MOUSE_CLICK.
        if (ev.buttons & 1) {
            left_down = true;
        }

        // Accumulate into a target position (sub-pixel).
        mouse_target_x16 += (int32_t)(ev.dx * MOUSE_SENSITIVITY) << 16;
        mouse_target_y16 += (int32_t)(ev.dy * MOUSE_SENSITIVITY) << 16;

        // Clamp target to screen bounds
        const int w = Compositor::GetWidth();
        const int h = Compositor::GetHeight();
        const int32_t min_x16 = 0;
        const int32_t min_y16 = 0;
        const int32_t max_x16 = (w > 0) ? ((int32_t)(w - 1) << 16) : 0;
        const int32_t max_y16 = (h > 0) ? ((int32_t)(h - 1) << 16) : 0;
        if (mouse_target_x16 < min_x16) mouse_target_x16 = min_x16;
        if (mouse_target_y16 < min_y16) mouse_target_y16 = min_y16;
        if (mouse_target_x16 > max_x16) mouse_target_x16 = max_x16;
        if (mouse_target_y16 > max_y16) mouse_target_y16 = max_y16;
    }
}

static void UpdateMousePerFrame() {
    // Goal: minimal lag. We only smooth tiny jitter; real movement snaps to target.
    const int32_t dx16 = mouse_target_x16 - mouse_x16;
    const int32_t dy16 = mouse_target_y16 - mouse_y16;

    // If we're dragging, always snap.
    if (left_down) {
        mouse_x16 = mouse_target_x16;
        mouse_y16 = mouse_target_y16;
    } else {
        // If we're more than ~1px away, snap to avoid trailing lag.
        const int32_t snap_threshold16 = (1 << 16);
        if (dx16 > snap_threshold16 || dx16 < -snap_threshold16 ||
            dy16 > snap_threshold16 || dy16 < -snap_threshold16) {
            mouse_x16 = mouse_target_x16;
            mouse_y16 = mouse_target_y16;
        } else {
            // Smooth small sub-pixel noise: alpha = 1/2.
            mouse_x16 += (dx16 >> 1);
            mouse_y16 += (dy16 >> 1);
            // Snap the last fraction to eliminate tail drift.
            const int32_t tail16 = (1 << 14); // 1/4 px
            if (dx16 > -tail16 && dx16 < tail16) mouse_x16 = mouse_target_x16;
            if (dy16 > -tail16 && dy16 < tail16) mouse_y16 = mouse_target_y16;
        }
    }

    const int32_t render_x16 = mouse_x16;
    const int32_t render_y16 = mouse_y16;

    mouse_x = (int)(render_x16 >> 16);
    mouse_y = (int)(render_y16 >> 16);

    if (left_down) {
        if (drag_target_idx != -1) {
            windows[drag_target_idx].x = mouse_x - drag_offset_x;
            windows[drag_target_idx].y = mouse_y - drag_offset_y;
        }
    }
}

void PollInput() {
    OSEvent ev;
    // Drain more events per frame to avoid backlog/jumps when mouse moves fast.
    int count = 0;
    while (sys_get_event(&ev) && count < 512) {
        HandleEvent(ev);
        count++;
    }
    UpdateMousePerFrame();
}

extern "C" int main(void* asset_ptr) {
    (void)asset_ptr;
    if (!Compositor::Initialize()) return -1;

    // Initialize smoothing state.
    mouse_x16 = ((int32_t)mouse_x) << 16;
    mouse_y16 = ((int32_t)mouse_y) << 16;
    mouse_target_x16 = mouse_x16;
    mouse_target_y16 = mouse_y16;
    
    InitWindows();
    
    const int TARGET_FPS = 60;
    const int FRAME_TIME_MS = 1000 / TARGET_FPS;
    
    while (1) {
        uint64_t start_time = sys_get_time_ms();
        
        // A. Input
        PollInput();
        
        // C. Render
        Compositor::RenderScene(windows, window_count, mouse_x, mouse_y);
        
        // D. Swap
        const bool vsynced = Compositor::SwapBuffers();
        
        // E. Sync
        // If present already waited for VSync, don't add extra sleep (avoids 1-frame lag).
        if (!vsynced) {
            uint64_t end_time = sys_get_time_ms();
            uint64_t elapsed = end_time - start_time;
            if (elapsed < (uint64_t)FRAME_TIME_MS) {
                sys_sleep((uint32_t)(FRAME_TIME_MS - elapsed));
            }
        }
    }
    return 0;
}
