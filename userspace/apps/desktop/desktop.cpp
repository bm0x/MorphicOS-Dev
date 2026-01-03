#include "os_event.h"
#include "compositor.h"
#include "system_info.h"

// Syscall stubs not covered by compositor.h
extern "C" {
    uint64_t sys_get_time_ms(); 
    void  sys_sleep(uint32_t ms);
    int   sys_get_event(OSEvent* ev); // Returns 1 if event, 0 if none
    int   sys_get_rtc_datetime(MorphicDateTime* out);
    int   sys_get_system_info(MorphicSystemInfo* out);
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

static bool menu_open = false;
static bool ui_dirty = false;

static MorphicDateTime g_rtc = {};
static MorphicSystemInfo g_sys = {};
static uint64_t g_last_rtc_ms = 0;
static uint32_t g_last_clock_sec = 0;

// Interaction State
int drag_target_idx = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;

void InitWindows() {
    windows[0] = {100, 100, 420, 300, 0xFF4040A0, "File Manager", false, false, false, 0,0,0,0};
    windows[1] = {560, 150, 320, 220, 0xFF40A040, "Terminal", false, false, false, 0,0,0,0};
    windows[2] = {160, 160, 520, 260, 0xFF4080A0, "System", false, false, false, 0,0,0,0};
    window_count = 3;
}

static void BringToFront(int idx) {
    if (idx < 0 || idx >= window_count) return;
    if (idx == window_count - 1) return;
    Compositor::Window temp = windows[idx];
    for (int i = idx; i < window_count - 1; i++) {
        windows[i] = windows[i + 1];
    }
    windows[window_count - 1] = temp;
    ui_dirty = true;
}

static bool HitRect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void RefreshTimeAndSysinfo() {
    uint64_t now = sys_get_time_ms();
    if (g_last_rtc_ms == 0 || (now - g_last_rtc_ms) >= 250) {
        // Read RTC periodically; it is cheap but we still avoid doing it every frame.
        MorphicDateTime dt;
        if (sys_get_rtc_datetime(&dt) == 1 && dt.valid) {
            g_rtc = dt;
            g_last_rtc_ms = now;
        }
    }

    static bool sysinfo_once = false;
    if (!sysinfo_once) {
        MorphicSystemInfo si;
        if (sys_get_system_info(&si) == 1) {
            g_sys = si;
            sysinfo_once = true;
            ui_dirty = true;
        }
    }
}

static uint32_t GetClockSeconds() {
    // Prefer RTC if valid, else fall back to monotonic time.
    if (g_rtc.valid) {
        return (uint32_t)g_rtc.hour * 3600u + (uint32_t)g_rtc.minute * 60u + (uint32_t)g_rtc.second;
    }
    return (uint32_t)(sys_get_time_ms() / 1000ULL);
}

static void UppercaseInplace(char* s) {
    if (!s) return;
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - ('a' - 'A'));
        // Replace unsupported punctuation with space to keep font simple.
        char c = *s;
        bool ok = (c == ' ' || c == ':' || c == '-' || c == '/' || c == '.' ||
                   (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'));
        if (!ok) *s = ' ';
    }
}

void HandleEvent(const OSEvent& ev) {
    if (ev.type == OSEvent::MOUSE_CLICK) {
        // Kernel sends current button state on transitions.
        left_down = (ev.buttons & 1);
        if (!left_down) {
            drag_target_idx = -1;
        } else {
            // Start drag immediately on click-down (no movement required).
            const int click_x = (int)(mouse_target_x16 >> 16);
            const int click_y = (int)(mouse_target_y16 >> 16);

            // Taskbar interactions
            const int taskH = 40;
            const int taskY = Compositor::GetHeight() - taskH;
            if (click_y >= taskY) {
                // Menu button
                if (HitRect(click_x, click_y, 10, taskY + 8, 28, 24)) {
                    menu_open = !menu_open;
                    ui_dirty = true;
                    return;
                }

                // Window icons
                int iconX = 48;
                for (int i = 0; i < window_count; i++) {
                    if (HitRect(click_x, click_y, iconX, taskY + 8, 24, 24)) {
                        // Toggle minimize/restore and bring to front
                        windows[i].minimized = !windows[i].minimized;
                        ui_dirty = true;
                        if (!windows[i].minimized) {
                            BringToFront(i);
                        }
                        return;
                    }
                    iconX += 30;
                }
                return;
            }

            // Window interactions (topmost first)
            const int titleH = 26;
            const int btnSize = 14;
            const int pad = 6;
            for (int i = window_count - 1; i >= 0; i--) {
                Compositor::Window& w = windows[i];
                if (w.minimized) continue;

                // Controls on titlebar right
                int by = w.y + (titleH - btnSize) / 2;
                int bxClose = w.x + w.width - pad - btnSize;
                int bxMax = bxClose - (btnSize + 6);
                int bxMin = bxMax - (btnSize + 6);

                if (HitRect(click_x, click_y, bxClose, by, btnSize, btnSize)) {
                    // Close: minimize for now (keeps demo windows)
                    w.minimized = true;
                    ui_dirty = true;
                    drag_target_idx = -1;
                    return;
                }
                if (HitRect(click_x, click_y, bxMax, by, btnSize, btnSize)) {
                    // Toggle maximize
                    const int taskH2 = 40;
                    const int maxW = Compositor::GetWidth();
                    const int maxH = Compositor::GetHeight() - taskH2;
                    if (!w.maximized) {
                        w.restore_x = w.x;
                        w.restore_y = w.y;
                        w.restore_w = w.width;
                        w.restore_h = w.height;
                        w.x = 0;
                        w.y = 0;
                        w.width = maxW;
                        w.height = maxH;
                        w.maximized = true;
                    } else {
                        w.x = w.restore_x;
                        w.y = w.restore_y;
                        w.width = w.restore_w;
                        w.height = w.restore_h;
                        w.maximized = false;
                    }
                    ui_dirty = true;
                    BringToFront(i);
                    return;
                }
                if (HitRect(click_x, click_y, bxMin, by, btnSize, btnSize)) {
                    w.minimized = true;
                    ui_dirty = true;
                    drag_target_idx = -1;
                    return;
                }

                if (click_x >= w.x && click_x < w.x + w.width &&
                    click_y >= w.y && click_y < w.y + 25) {
                    drag_target_idx = i;
                    drag_offset_x = click_x - w.x;
                    drag_offset_y = click_y - w.y;

                    BringToFront(i);
                    drag_target_idx = window_count - 1;
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

struct DirtyRect {
    int x, y, w, h;
    bool valid;
};

static void DirtyInit(DirtyRect& r) { r.valid = false; r.x = r.y = r.w = r.h = 0; }
static void DirtyAdd(DirtyRect& r, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!r.valid) { r.x = x; r.y = y; r.w = w; r.h = h; r.valid = true; return; }
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;
    int nx = (x < r.x) ? x : r.x;
    int ny = (y < r.y) ? y : r.y;
    int nx2 = (x + w > x2) ? (x + w) : x2;
    int ny2 = (y + h > y2) ? (y + h) : y2;
    r.x = nx; r.y = ny; r.w = nx2 - nx; r.h = ny2 - ny;
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

    int prev_mouse_x = mouse_x;
    int prev_mouse_y = mouse_y;
    uint32_t prev_clock_sec = 0;
    bool prev_menu_open = menu_open;

    // Track last known window rects to compute dirty on moves.
    int prev_wx[MAX_WINDOWS];
    int prev_wy[MAX_WINDOWS];
    int prev_ww[MAX_WINDOWS];
    int prev_wh[MAX_WINDOWS];
    bool prev_min[MAX_WINDOWS];
    bool prev_max[MAX_WINDOWS];
    for (int i = 0; i < window_count; i++) {
        prev_wx[i] = windows[i].x;
        prev_wy[i] = windows[i].y;
        prev_ww[i] = windows[i].width;
        prev_wh[i] = windows[i].height;
        prev_min[i] = windows[i].minimized;
        prev_max[i] = windows[i].maximized;
    }

    bool first_frame = true;
    
    while (1) {
        uint64_t start_time = sys_get_time_ms();

        DirtyRect dirty;
        DirtyInit(dirty);
        
        // A. Input
        PollInput();

        // B. Time/sysinfo refresh
        RefreshTimeAndSysinfo();

        // Force a full redraw at startup (ensures the desktop paints 100%).
        if (first_frame) {
            DirtyAdd(dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        }

        // Cursor dirty (old + new)
        DirtyAdd(dirty, prev_mouse_x - 2, prev_mouse_y - 2, 20, 24);
        DirtyAdd(dirty, mouse_x - 2, mouse_y - 2, 20, 24);

        // Window moves (drag/max/min)
        for (int i = 0; i < window_count; i++) {
            if (windows[i].minimized != prev_min[i] || windows[i].maximized != prev_max[i]) {
                // Minimize/restore doesn't change geometry, but it changes visibility.
                DirtyAdd(dirty, prev_wx[i] - 2, prev_wy[i] - 2, prev_ww[i] + 4, prev_wh[i] + 4);
                DirtyAdd(dirty, windows[i].x - 2, windows[i].y - 2, windows[i].width + 4, windows[i].height + 4);
                ui_dirty = true;
            }
            if (windows[i].x != prev_wx[i] || windows[i].y != prev_wy[i] ||
                windows[i].width != prev_ww[i] || windows[i].height != prev_wh[i]) {
                DirtyAdd(dirty, prev_wx[i] - 2, prev_wy[i] - 2, prev_ww[i] + 4, prev_wh[i] + 4);
                DirtyAdd(dirty, windows[i].x - 2, windows[i].y - 2, windows[i].width + 4, windows[i].height + 4);
            }
        }

        // Taskbar/menu changes
        uint32_t now_sec = GetClockSeconds();
        if (now_sec != prev_clock_sec || menu_open != prev_menu_open) {
            int taskH = 40;
            DirtyAdd(dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            // menu area
            if (menu_open || prev_menu_open) {
                DirtyAdd(dirty, 0, Compositor::GetHeight() - taskH - 200, 260, 220);
            }
        }

        // If RTC second changed, update the taskbar clock.
        if (now_sec != g_last_clock_sec) {
            int taskH = 40;
            DirtyAdd(dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            g_last_clock_sec = now_sec;
        }

        // Any UI reorder/state change: repaint taskbar/menu.
        if (ui_dirty) {
            int taskH = 40;
            DirtyAdd(dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            if (menu_open || prev_menu_open) {
                DirtyAdd(dirty, 0, Compositor::GetHeight() - taskH - 200, 260, 220);
            }
        }
        
        // C. Render
        // If nothing marked dirty (rare), still redraw cursor region.
        if (!dirty.valid) {
            DirtyAdd(dirty, mouse_x - 2, mouse_y - 2, 20, 24);
        }

        // Redraw only dirty region: background + UI + windows + cursor.
        // (Compositor is clip-aware internally.)
        // Set clip via SwapBuffersRect usage: we redraw full but clipped.
        // We reuse RenderScene for windows/cursor and draw taskbar/menu explicitly.
        Compositor::SetClip(dirty.x, dirty.y, dirty.w, dirty.h);
        // Full render within clip
        // Background is drawn inside RenderScene
        Compositor::RenderScene(windows, window_count, mouse_x, mouse_y);
        Compositor::RenderTaskbar(windows, window_count, menu_open, now_sec);
        Compositor::RenderMenu(menu_open);

        // System window content (text-based, lightweight)
        for (int i = 0; i < window_count; i++) {
            Compositor::Window& w = windows[i];
            if (w.minimized) continue;
            if (!w.title) continue;
            // Match "System"
            if (w.title[0] != 'S') continue;

            int pad = 16;
            int titleH = 26;
            int x = w.x + pad;
            int y = w.y + titleH + pad;

            char vendor[13];
            for (int k = 0; k < 13; k++) vendor[k] = g_sys.cpu_vendor[k];
            vendor[12] = '\0';
            UppercaseInplace(vendor);

            char line1[64];
            // "MORPHIC OS"
            const char* hdr = "MORPHIC OS";
            // Draw header
            Compositor::DrawText(x, y, hdr, 0xFFEAEAEA, 1);
            y += 14;

            // CPU vendor
            // Build "CPU VENDOR: <vendor>"
            int idx = 0;
            const char* p = "CPU VENDOR: ";
            while (*p && idx < 63) line1[idx++] = *p++;
            for (int k = 0; vendor[k] && idx < 63; k++) line1[idx++] = vendor[k];
            line1[idx] = '\0';
            Compositor::DrawText(x, y, line1, 0xFFCCCCCC, 1);
            y += 12;

            // RAM
            uint64_t totalMiB = g_sys.total_mem_bytes / (1024ULL * 1024ULL);
            uint64_t freeMiB  = g_sys.free_mem_bytes / (1024ULL * 1024ULL);
            char line2[64];
            // "RAM: TTTTMB FREE: FFFMB"
            // Minimal integer formatting
            auto write_u64 = [](char* out, int& pos, uint64_t v) {
                char tmp[24];
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
                for (int i = n - 1; i >= 0; i--) out[pos++] = tmp[i];
            };

            int pos = 0;
            const char* a = "RAM: ";
            while (*a && pos < 63) line2[pos++] = *a++;
            write_u64(line2, pos, totalMiB);
            const char* b = "MB FREE: ";
            while (*b && pos < 63) line2[pos++] = *b++;
            write_u64(line2, pos, freeMiB);
            const char* c = "MB";
            while (*c && pos < 63) line2[pos++] = *c++;
            line2[pos] = '\0';
            UppercaseInplace(line2);
            Compositor::DrawText(x, y, line2, 0xFFCCCCCC, 1);
            y += 12;

            // Video
            char line3[64];
            pos = 0;
            const char* v0 = "VIDEO: ";
            while (*v0 && pos < 63) line3[pos++] = *v0++;
            write_u64(line3, pos, g_sys.fb_width);
            line3[pos++] = 'X';
            write_u64(line3, pos, g_sys.fb_height);
            const char* v1 = " PITCH: ";
            while (*v1 && pos < 63) line3[pos++] = *v1++;
            write_u64(line3, pos, g_sys.fb_pitch);
            line3[pos] = '\0';
            UppercaseInplace(line3);
            Compositor::DrawText(x, y, line3, 0xFFCCCCCC, 1);
            y += 12;

            // RTC time/date
            if (g_rtc.valid) {
                char line4[64];
                pos = 0;
                const char* r0 = "RTC: ";
                while (*r0 && pos < 63) line4[pos++] = *r0++;
                write_u64(line4, pos, g_rtc.year);
                line4[pos++] = '-';
                line4[pos++] = (char)('0' + (g_rtc.month / 10) % 10);
                line4[pos++] = (char)('0' + (g_rtc.month % 10));
                line4[pos++] = '-';
                line4[pos++] = (char)('0' + (g_rtc.day / 10) % 10);
                line4[pos++] = (char)('0' + (g_rtc.day % 10));
                line4[pos++] = ' ';
                line4[pos++] = (char)('0' + (g_rtc.hour / 10) % 10);
                line4[pos++] = (char)('0' + (g_rtc.hour % 10));
                line4[pos++] = ':';
                line4[pos++] = (char)('0' + (g_rtc.minute / 10) % 10);
                line4[pos++] = (char)('0' + (g_rtc.minute % 10));
                line4[pos++] = ':';
                line4[pos++] = (char)('0' + (g_rtc.second / 10) % 10);
                line4[pos++] = (char)('0' + (g_rtc.second % 10));
                line4[pos] = '\0';
                UppercaseInplace(line4);
                Compositor::DrawText(x, y, line4, 0xFFCCCCCC, 1);
            }
            break;
        }

        // Cursor must be last so it stays above taskbar/menu.
        Compositor::DrawCursor(mouse_x, mouse_y);
        Compositor::ClearClip();

        // D. Swap only dirty region
        const bool vsynced = Compositor::SwapBuffersRect(dirty.x, dirty.y, dirty.w, dirty.h);
        
        // E. Sync
        // If present already waited for VSync, don't add extra sleep (avoids 1-frame lag).
        if (!vsynced) {
            uint64_t end_time = sys_get_time_ms();
            uint64_t elapsed = end_time - start_time;
            if (elapsed < (uint64_t)FRAME_TIME_MS) {
                sys_sleep((uint32_t)(FRAME_TIME_MS - elapsed));
            }
        }

        // Update prev state
        prev_mouse_x = mouse_x;
        prev_mouse_y = mouse_y;
        prev_clock_sec = now_sec;
        prev_menu_open = menu_open;
        first_frame = false;
        ui_dirty = false;
        for (int i = 0; i < window_count; i++) {
            prev_wx[i] = windows[i].x;
            prev_wy[i] = windows[i].y;
            prev_ww[i] = windows[i].width;
            prev_wh[i] = windows[i].height;
            prev_min[i] = windows[i].minimized;
            prev_max[i] = windows[i].maximized;
        }
    }
    return 0;
}
