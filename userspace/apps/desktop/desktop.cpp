#include "os_event.h"
#include "compositor.h"
#include "system_info.h"
#include "launcher.h"
#include "morphic_syscalls.h"
#include "graphics_uapi.h"
#include "../../sdk/compositor_protocol.h"

// Syscall stubs not covered by compositor.h
// Syscall stubs are now in morphic_syscalls.h

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
static constexpr int CURSOR_W = 12;
static constexpr int CURSOR_H = 18;
static constexpr int CURSOR_PAD = 2;
static constexpr int CURSOR_DIRTY_W = CURSOR_W + (CURSOR_PAD * 2);
static constexpr int CURSOR_DIRTY_H = CURSOR_H + (CURSOR_PAD * 2);

// Globals for Windows
#define MAX_WINDOWS 10
Compositor::Window windows[MAX_WINDOWS];
int window_count = 0;

static bool menu_open = false;
static bool ui_dirty = false;
static uint64_t g_menu_open_ms = 0;
static uint64_t g_last_menu_toggle_ms = 0;

static MorphicDateTime g_rtc = {};
static MorphicSystemInfo g_sys = {};
static uint64_t g_last_rtc_ms = 0;
static uint32_t g_last_clock_sec = 0;
static uint64_t g_last_render_time = 0;
static uint32_t g_last_polled_events = 0;
static uint64_t g_last_input_drop_count = 0;
static uint64_t g_last_drop_poll_ms = 0;
static uint64_t g_startup_time_ms = 0;
static bool g_has_atomic_commit = false;
static uint32_t g_last_display_events = 0;
static uint64_t g_last_vblank_sequence = 0;
static uint64_t g_last_flip_sequence = 0;
static uint64_t g_last_client_pid = 0;
static uint32_t g_last_client_message = 0;
static uint32_t g_registered_client_count = 0;

static constexpr int MAX_PROTOCOL_CLIENTS = 16;
static uint64_t g_protocol_clients[MAX_PROTOCOL_CLIENTS] = {};

struct ProtocolSurface {
    bool active;
    uint64_t client_pid;
    uint64_t bound_window_id;
    uint32_t width;
    uint32_t height;
    uint64_t commit_count;
    uint64_t last_commit_ms;
};

static ProtocolSurface g_protocol_surfaces[MAX_PROTOCOL_CLIENTS] = {};
static uint32_t g_active_surface_count = 0;
static uint64_t g_total_surface_commits = 0;

static Launcher g_launcher;

// Interaction State
int drag_target_idx = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;

void InitWindows() {
    windows[0] = {160, 160, 520, 260, 0xFF4080A0, "System", false, false, false, 0,0,0,0};
    window_count = 1;
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

void OpenWindow(const char* title, int w, int h, uint32_t color) {
    if (window_count >= MAX_WINDOWS) return;
    
    // Check if already open
    for (int i = 0; i < window_count; i++) {
        if (windows[i].title && windows[i].title[0] == title[0]) { // Simple check
             // Bring to front
             windows[i].minimized = false;
             BringToFront(i);
             return;
        }
    }

    windows[window_count] = {
        100 + (window_count * 20), 
        100 + (window_count * 20), 
        w, h, color, title, 
        false, false, false, 0,0,0,0
    };
    window_count++;
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

    if (g_last_drop_poll_ms == 0 || (now - g_last_drop_poll_ms) >= 1000) {
        g_last_input_drop_count = sys_get_input_drop_count();
        g_last_drop_poll_ms = now;
    }
}

static uint32_t GetClockSeconds() {
    // Prefer RTC if valid, else fall back to monotonic time.
    if (g_rtc.valid) {
        return (uint32_t)g_rtc.hour * 3600u + (uint32_t)g_rtc.minute * 60u + (uint32_t)g_rtc.second;
    }
    return (uint32_t)(sys_get_time_ms() / 1000ULL);
}

void UppercaseInplace(char* s) {
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

// External Windows (Calculator, Terminal, etc.) retrieved from Kernel
const int MAX_EXT_WINDOWS = 32;
WindowInfo externalWindows[MAX_EXT_WINDOWS];
uint32_t externalWindowCount = 0;
uint64_t drag_target_external_id = 0;
uint64_t active_pid = 0;

// Track previous external window positions for dirty rect calculation
static WindowInfo prevExternalWindows[MAX_EXT_WINDOWS];
static uint32_t prevExternalWindowCount = 0;

void FetchExternalWindows() {
    externalWindowCount = sys_get_window_list(externalWindows, MAX_EXT_WINDOWS);
}

static int FindExternalWindowIndexById(uint64_t id) {
    for (uint32_t i = 0; i < externalWindowCount; i++) {
        if (externalWindows[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int FindExternalWindowIndexByPid(uint64_t pid) {
    for (int i = (int)externalWindowCount - 1; i >= 0; i--) {
        if (externalWindows[(uint32_t)i].pid == pid && externalWindows[(uint32_t)i].flags) {
            return i;
        }
    }
    return -1;
}

static void DebugProtocolMessage(const char* prefix, uint64_t pid, uint32_t a, uint32_t b);

// Global Keymap State
static int currentKeymapIndex = 0;

static const char* GetKeymapName(int idx) {
    if (idx == 1) return "ES";
    if (idx == 2) return "LA";
    return "US";
}

void CycleKeymap() {
    currentKeymapIndex = (currentKeymapIndex + 1) % 3;
    const char* newMap = GetKeymapName(currentKeymapIndex);
    sys_set_keymap(newMap);
    sys_debug_print("Switched Keymap to: ");
    sys_debug_print(newMap);
    sys_debug_print("\n");
    ui_dirty = true;
}

// Dirty Rect Tracking (moved up for use in HandleEvent)
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

static DirtyRect g_dirty;

static void RecountProtocolSurfaces() {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        if (g_protocol_surfaces[i].active) {
            count++;
        }
    }
    g_active_surface_count = count;
}

static int FindProtocolSurfaceIndexByPid(uint64_t pid) {
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        if (g_protocol_surfaces[i].active && g_protocol_surfaces[i].client_pid == pid) {
            return i;
        }
    }
    return -1;
}

static ProtocolSurface* EnsureProtocolSurface(uint64_t pid, uint32_t width, uint32_t height) {
    if (pid == 0) {
        return nullptr;
    }

    int idx = FindProtocolSurfaceIndexByPid(pid);
    if (idx < 0) {
        for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
            if (!g_protocol_surfaces[i].active) {
                idx = i;
                g_protocol_surfaces[i] = {};
                g_protocol_surfaces[i].active = true;
                g_protocol_surfaces[i].client_pid = pid;
                break;
            }
        }
    }

    if (idx < 0) {
        return nullptr;
    }

    if (width > 0) {
        g_protocol_surfaces[idx].width = width;
    }
    if (height > 0) {
        g_protocol_surfaces[idx].height = height;
    }

    RecountProtocolSurfaces();
    return &g_protocol_surfaces[idx];
}

static void MarkProtocolSurfaceDirty(const ProtocolSurface* surface) {
    if (!surface || !surface->active) {
        return;
    }

    int idx = FindExternalWindowIndexById(surface->bound_window_id);
    if (idx < 0) {
        DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        return;
    }

    const WindowInfo& w = externalWindows[(uint32_t)idx];
    DirtyAdd(g_dirty, (int)w.x - 2, (int)w.y - 26, (int)w.w + 4, (int)w.h + 30);
}

static void BindProtocolSurfacesToExternalWindows() {
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        ProtocolSurface& surface = g_protocol_surfaces[i];
        if (!surface.active) {
            continue;
        }

        int bound_idx = -1;
        if (surface.bound_window_id != 0) {
            bound_idx = FindExternalWindowIndexById(surface.bound_window_id);
        }

        if (bound_idx < 0) {
            int pid_idx = FindExternalWindowIndexByPid(surface.client_pid);
            if (pid_idx >= 0) {
                WindowInfo& w = externalWindows[(uint32_t)pid_idx];
                surface.bound_window_id = w.id;
                if (surface.width == 0) surface.width = w.w;
                if (surface.height == 0) surface.height = w.h;
                DebugProtocolMessage("[Desktop] Surface bound", surface.client_pid, surface.width, surface.height);
                MarkProtocolSurfaceDirty(&surface);
            } else {
                surface.bound_window_id = 0;
            }
        } else {
            WindowInfo& w = externalWindows[(uint32_t)bound_idx];
            surface.width = w.w;
            surface.height = w.h;
        }
    }
}

static void DrawProtocolSurfaceBadges() {
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        const ProtocolSurface& surface = g_protocol_surfaces[i];
        if (!surface.active || surface.bound_window_id == 0) {
            continue;
        }

        int ext_idx = FindExternalWindowIndexById(surface.bound_window_id);
        if (ext_idx < 0) {
            continue;
        }

        const WindowInfo& w = externalWindows[(uint32_t)ext_idx];
        int bx = (int)w.x + 6;
        int by = (int)w.y - 22;
        if (by < 2) by = 2;

        Compositor::DrawRect(bx, by, 14, 14, 0xFF2B4A66);
        Compositor::DrawText(bx + 4, by + 3, "P", 0xFFEAF2FF, 1);
    }
}

static void AppendU64(char* out, int& pos, int max_len, uint64_t value) {
    char tmp[24];
    int count = 0;
    if (value == 0) {
        tmp[count++] = '0';
    }
    while (value && count < 23) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && pos < max_len - 1) {
        out[pos++] = tmp[--count];
    }
}

static void DebugProtocolMessage(const char* prefix, uint64_t pid, uint32_t a, uint32_t b) {
    char line[96];
    int pos = 0;
    while (*prefix && pos < 95) line[pos++] = *prefix++;
    const char* pid_label = " PID=";
    while (*pid_label && pos < 95) line[pos++] = *pid_label++;
    AppendU64(line, pos, 96, pid);
    const char* size_label = " SIZE=";
    while (*size_label && pos < 95) line[pos++] = *size_label++;
    AppendU64(line, pos, 96, a);
    if (pos < 95) line[pos++] = 'x';
    AppendU64(line, pos, 96, b);
    if (pos < 95) line[pos++] = '\n';
    line[pos < 95 ? pos : 95] = 0;
    sys_debug_print(line);
}

static void RegisterProtocolClient(uint64_t pid) {
    if (pid == 0) return;
    for (uint32_t i = 0; i < g_registered_client_count; i++) {
        if (g_protocol_clients[i] == pid) {
            return;
        }
    }
    if (g_registered_client_count >= MAX_PROTOCOL_CLIENTS) {
        return;
    }
    g_protocol_clients[g_registered_client_count++] = pid;
}

static bool HandleProtocolMessage(const OSEvent& ev) {
    CompositorMessage msg = {};
    if (!MorphicCompositor::DecodeMessage(ev, &msg)) {
        return false;
    }

    uint64_t client_pid = (msg.arg0 > 0) ? (uint64_t)(uint32_t)msg.arg0 : 0;
    g_last_client_pid = client_pid;
    g_last_client_message = msg.type;

    switch (msg.type) {
    case COMPOSITOR_MSG_HELLO:
        RegisterProtocolClient(client_pid);
        EnsureProtocolSurface(client_pid, msg.arg2, msg.arg3);
        DebugProtocolMessage("[Desktop] Client HELLO", client_pid, msg.arg2, msg.arg3);
        ui_dirty = true;
        return true;
    case COMPOSITOR_MSG_CREATE_SURFACE:
        RegisterProtocolClient(client_pid);
        if (ProtocolSurface* surface = EnsureProtocolSurface(client_pid, msg.arg2, msg.arg3)) {
            surface->last_commit_ms = sys_get_time_ms();
            MarkProtocolSurfaceDirty(surface);
        }
        DebugProtocolMessage("[Desktop] Client CREATE_SURFACE", client_pid, msg.arg2, msg.arg3);
        return true;
    case COMPOSITOR_MSG_COMMIT_SURFACE:
        RegisterProtocolClient(client_pid);
        if (ProtocolSurface* surface = EnsureProtocolSurface(client_pid, msg.arg2, msg.arg3)) {
            surface->commit_count++;
            surface->last_commit_ms = sys_get_time_ms();
            g_total_surface_commits++;
            MarkProtocolSurfaceDirty(surface);
        }
        return true;
    case COMPOSITOR_MSG_SET_FOCUS:
        active_pid = client_pid;
        return true;
    default:
        return false;
    }
}

void HandleEvent(const OSEvent& ev) {
    if (ev.type == OSEvent::USER_MESSAGE) {
        if (HandleProtocolMessage(ev)) {
            return;
        }
    }

    // Pass keyboard events to the focused window (topmost)
    if (ev.type == OSEvent::KEY_PRESS) {
        if (active_pid != 0) {
            OSEvent fwd = ev; // Copy
            sys_post_message(active_pid, &fwd);
            return;
        }
        if (window_count > 0) {
            Compositor::Window& top = windows[window_count - 1];
            if (!top.minimized && top.title) {
                // Legacy internal apps removed

            }
        }
    }

    if (ev.type == OSEvent::MOUSE_CLICK) {
        // Kernel sends current button state on transitions.
        const bool was_down = left_down;
        left_down = (ev.buttons & 1);

        // Ignore repeated "press" packets without an intervening release.
        if (left_down && was_down) {
            return;
        }
        
        // Only process on button DOWN transition (not release)
        if (!left_down) {
            drag_target_idx = -1;
            drag_target_external_id = 0;
            return;
        }
        
        // Calculate click position from smooth mouse
        const int click_x = (int)(mouse_target_x16 >> 16);
        const int click_y = (int)(mouse_target_y16 >> 16);
        
        // Reset drag state
        drag_target_idx = -1;
        drag_target_external_id = 0;

        // Startup grace period: ignore all click actions for the first 500ms.
        // Prevents phantom clicks queued by QEMU/firmware during kernel boot
        // from triggering UI actions (launcher open → shutdown) immediately.
        if ((sys_get_time_ms() - g_startup_time_ms) < 500) {
            return;
        }

        // ========================================
        // PRIORITY 1: Launcher (captures ALL clicks when open)
        // ========================================
        if (menu_open) {
            uint64_t now_click_ms = sys_get_time_ms();
            // Debounce opening click: prevents immediate close/reopen loops.
            if ((now_click_ms - g_menu_open_ms) < 140) {
                return;
            }

            sys_debug_print("[Desktop] Click while launcher open\n");
            
            // CRITICAL: Close launcher FIRST before any action
            // This ensures the UI updates even if spawn causes preemption
            menu_open = false;
            g_last_menu_toggle_ms = now_click_ms;
            ui_dirty = true;
            
            // Force full screen dirty immediately so the next render clears launcher
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
            
            // Now handle the click (may spawn app)
            bool spawned = g_launcher.HandleClick(click_x, click_y);
            g_launcher.spawnPending = false;
            
            if (spawned) {
                sys_debug_print("[Desktop] App spawned, yielding to allow frame completion\n");
            }
            
            sys_debug_print("[Desktop] Launcher closed, menu_open=false\n");
            return;
        }

        // ========================================
        // PRIORITY 2: Taskbar
        // ========================================
        const int taskH = 40;
        const int taskY = Compositor::GetHeight() - taskH;
        if (click_y >= taskY) {
            int width = Compositor::GetWidth();
            
            // Keymap Toggle
            if (HitRect(click_x, click_y, width - 130, taskY + 10, 40, 30)) {
                CycleKeymap();
                return;
            }

            // Menu button (toggle launcher)
            if (HitRect(click_x, click_y, 10, taskY + 8, 28, 24)) {
                uint64_t now_toggle_ms = sys_get_time_ms();
                if ((now_toggle_ms - g_last_menu_toggle_ms) < 160) {
                    return;
                }
                menu_open = !menu_open;
                g_last_menu_toggle_ms = now_toggle_ms;
                if (menu_open) {
                    g_menu_open_ms = now_toggle_ms;
                    sys_debug_print("[Desktop] Launcher opened\n");
                }
                ui_dirty = true;
                active_pid = 0;
                return;
            }

            // Window icons
            int iconX = 48;
            for (int i = 0; i < window_count; i++) {
                if (HitRect(click_x, click_y, iconX, taskY + 8, 24, 24)) {
                    windows[i].minimized = !windows[i].minimized;
                    ui_dirty = true;
                    if (!windows[i].minimized) BringToFront(i);
                    return;
                }
                iconX += 30;
            }
            return; // Consumed by taskbar
        }

        // ========================================
        // PRIORITY 3: External Windows (apps)
        // ========================================
        const int titleH = 26;
              for (int i = (int)externalWindowCount - 1; i >= 0; i--) {
                  WindowInfo& w = externalWindows[(uint32_t)i];
                 if (!w.flags) continue; // Only visible
                 
                 // 1. Hit Test Buttons (Close, Max, Min) - CHECK FIRST
                 const int btnSize = 14;
                 const int pad = 6;
                 int by = w.y - titleH + (titleH - btnSize) / 2;
                 int bxClose = w.x + w.w - pad - btnSize;
                 int bxMax = bxClose - (btnSize + 6);
                 int bxMin = bxMax - (btnSize + 6);

                 // Close Button - Send WINDOW_DESTROYED to app and hide window
                 if (HitRect(click_x, click_y, bxClose, by, btnSize, btnSize)) {
                     // Send close event to the app so it can terminate
                     OSEvent closeEv;
                     closeEv.type = OSEvent::WINDOW_DESTROYED;
                     closeEv.dx = 0;
                     closeEv.dy = 0;
                     closeEv.buttons = 0;
                     sys_post_message(w.pid, &closeEv);
                     
                     // Also hide the window immediately
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 0; 
                     sys_update_window(w.id, xy, wh_flags);
                     return;
                 }
                 
                 // Minimize Button (Hide)
                 if (HitRect(click_x, click_y, bxMin, by, btnSize, btnSize)) {
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 0;
                     sys_update_window(w.id, xy, wh_flags);
                     return;
                 }

                 // 2. Hit Test Title Bar for Drag (If not button)
                 if (click_x >= (int)w.x && click_x < (int)(w.x + w.w) &&
                     click_y >= (int)w.y - titleH && click_y < (int)(w.y)) {
                     
                     drag_target_external_id = w.id;
                     drag_offset_x = click_x - w.x;
                     drag_offset_y = click_y - w.y;
                     active_pid = w.pid;
                     return;
                 }

                 // 3. Forward Client Area Clicks (Content)
                 if (click_x >= (int)w.x && click_x < (int)(w.x + w.w) &&
                     click_y >= (int)w.y && click_y < (int)(w.y + w.h)) {
                     
                     // Construct Event
                     OSEvent ev;
                     ev.type = OSEvent::MOUSE_CLICK;
                     ev.dx = click_x - w.x; // Local X
                     ev.dy = click_y - w.y; // Local Y
                     ev.buttons = 1; // Left Click
                     
                     sys_post_message(w.pid, &ev);
                     active_pid = w.pid; // Set Focus
                     return;
                 }
            }

            // Window interactions (topmost first)
            // titleH already defined above
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
        return; // End of MOUSE_CLICK handling
    }

    if (ev.type == OSEvent::MOUSE_MOVE) {
        // Keep drag state coherent even when click transition packets are delayed/lost.
        const bool move_left_down = (ev.buttons & 1) != 0;
        if (move_left_down != left_down) {
            left_down = move_left_down;
            if (!left_down) {
                drag_target_idx = -1;
                drag_target_external_id = 0;
            }
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

// DirtyRect definitions moved up before HandleEvent

static void UpdateMousePerFrame() {
    // OPTIMIZATION: Removed smoothing entirely - it was causing perceived lag
    // The mouse should always snap to target position for responsive feel
    mouse_x16 = mouse_target_x16;
    mouse_y16 = mouse_target_y16;

    const int32_t render_x16 = mouse_x16;
    const int32_t render_y16 = mouse_y16;

    mouse_x = (int)(render_x16 >> 16);
    mouse_y = (int)(render_y16 >> 16);

    if (left_down) {
        if (drag_target_idx != -1) {
            windows[drag_target_idx].x = mouse_x - drag_offset_x;
            windows[drag_target_idx].y = mouse_y - drag_offset_y;
        } else if (drag_target_external_id != 0) {
            // Drag External Window
            // We need to find the window to get current dims (we assume size doesn't change during drag for now)
            int targetX = mouse_x - drag_offset_x;
            int targetY = mouse_y - drag_offset_y;
            
            // Find in cache to get W/H
            uint32_t w = 300, h = 200; // Default fallback
            for(uint32_t i=0; i<externalWindowCount; i++) {
                if (externalWindows[i].id == drag_target_external_id) {
                    w = externalWindows[i].w;
                    h = externalWindows[i].h;
                    
                    // Mark OLD pos dirty (Trail fix)
                    DirtyAdd(g_dirty, externalWindows[i].x - 2, externalWindows[i].y - 26, w + 4, h + 30);
                    
                    // Update local cache
                    externalWindows[i].x = targetX;
                    externalWindows[i].y = targetY;
                    
                    // Mark NEW pos dirty
                    DirtyAdd(g_dirty, targetX - 2, targetY - 26, w + 4, h + 30);
                    
                    break;
                }
            }

            // Send to Kernel
            // packed_pos = (x << 32) | y
            // Layout: W (24) | H (24) | Flags (16)
            // We must set Flag=1 (Visible) when dragging!
            uint64_t packed_pos = ((uint64_t)(uint32_t)targetX << 32) | (uint32_t)targetY;
            uint64_t packed_size = ((uint64_t)w << 40) | ((uint64_t)h << 16) | 1;
            sys_update_window(drag_target_external_id, packed_pos, packed_size);

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
    g_last_polled_events = (uint32_t)count;
    UpdateMousePerFrame();
}

void PollDisplayEvents() {
    GraphicsUapiEvent ev = {};
    int count = 0;
    while (count < 64 && MorphicGfx::PollEvent(&ev)) {
        if (ev.type == GRAPHICS_EVENT_VBLANK) {
            g_last_vblank_sequence = ev.sequence;
        } else if (ev.type == GRAPHICS_EVENT_FLIP_COMPLETE) {
            g_last_flip_sequence = ev.sequence;
        }
        count++;
    }
    g_last_display_events = (uint32_t)count;
}

extern "C" int main(void* asset_ptr) {
    (void)asset_ptr;
    sys_debug_print("[Desktop] main: before Compositor::Initialize\n");
    bool ok = Compositor::Initialize();
    if (!ok) {
        sys_debug_print("[Desktop] Compositor::Initialize FAILED\n");
        return -1;
    }
    sys_debug_print("[Desktop] Compositor::Initialize OK\n");

    // Register as Compositor to receive all input and window events
    sys_register_compositor();
    sys_debug_print("[Desktop] Registered as Compositor\n");

    // Initialize smoothing state.
    mouse_x16 = ((int32_t)mouse_x) << 16;
    mouse_y16 = ((int32_t)mouse_y) << 16;
    mouse_target_x16 = mouse_x16;
    mouse_target_y16 = mouse_y16;
    
    InitWindows();
    g_launcher.Init();

    GraphicsUapiCaps gfx_caps = {};
    if (MorphicGfx::QueryCaps(&gfx_caps)) {
        g_has_atomic_commit = (gfx_caps.caps_flags & GRAPHICS_CAP_ATOMIC_COMMIT) != 0;
        if (g_has_atomic_commit) {
            sys_debug_print("[Desktop] Using DRM atomic commit\n");
        }
    }

    // Drain all input events accumulated during kernel boot before entering
    // the render loop. QEMU / firmware can leave stale mouse/click events in
    // the queue that would otherwise trigger phantom UI actions on frame 0.
    {
        OSEvent _drain;
        int _n = 0;
        while (sys_get_event(&_drain) && _n < 2048) _n++;
        if (_n > 0) {
            sys_debug_print("[Desktop] Drained stale boot events\n");
        }
    }
    g_startup_time_ms = sys_get_time_ms();
    // Prime debounce timestamps to NOW so the gap from 0 doesn't bypass them.
    g_menu_open_ms       = g_startup_time_ms;
    g_last_menu_toggle_ms = g_startup_time_ms;

    if (sys_spawn("/initrd/desktop.mpk") == 0) {
        sys_debug_print("[Desktop] Spawned desktop client\n");
    }

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

    // DirtyRect dirty; - Using Global g_dirty
    DirtyInit(g_dirty);
        
        // Update External Windows List
        FetchExternalWindows();
        BindProtocolSurfacesToExternalWindows();
        
        // A. Input
        PollInput();

        // B. Time/sysinfo refresh
        RefreshTimeAndSysinfo();

        // Force a full redraw at startup (ensures the desktop paints 100%).
        if (first_frame) {
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        }

        // Decide flush strategy early so render clip matches the copy strategy.
        bool hasExternalWindows = (externalWindowCount > 0);
        bool needsFullFlush = menu_open || (!menu_open && prev_menu_open) || hasExternalWindows;

        // If we'll do a full flush, force full dirty so we don't copy stale backbuffer regions.
        if (needsFullFlush) {
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        }

        // Add External Windows to Dirty (so they get composed/swapped)
        // Compare current vs previous positions and mark both as dirty when changed
        for(uint32_t i=0; i<externalWindowCount; i++) {
            WindowInfo& w = externalWindows[i];
            if (w.flags) {
                // Current position
                DirtyAdd(g_dirty, w.x - 2, w.y - 26, w.w + 4, w.h + 30);
                
                // Check if this window moved from previous frame
                bool foundPrev = false;
                for (uint32_t j = 0; j < prevExternalWindowCount; j++) {
                    if (prevExternalWindows[j].id == w.id) {
                        foundPrev = true;
                        // If position changed, also mark the OLD position as dirty
                        if (prevExternalWindows[j].x != w.x || prevExternalWindows[j].y != w.y ||
                            prevExternalWindows[j].w != w.w || prevExternalWindows[j].h != w.h) {
                            DirtyAdd(g_dirty, prevExternalWindows[j].x - 2, prevExternalWindows[j].y - 26, 
                                     prevExternalWindows[j].w + 4, prevExternalWindows[j].h + 30);
                        }
                        break;
                    }
                }
                // New window - already marked dirty above
            }
        }
        
        // Mark closed windows as dirty (windows that existed before but not now)
        for (uint32_t i = 0; i < prevExternalWindowCount; i++) {
            bool stillExists = false;
            for (uint32_t j = 0; j < externalWindowCount; j++) {
                if (externalWindows[j].id == prevExternalWindows[i].id) {
                    stillExists = true;
                    break;
                }
            }
            if (!stillExists && prevExternalWindows[i].flags) {
                // Window was closed - mark its old position as dirty
                DirtyAdd(g_dirty, prevExternalWindows[i].x - 2, prevExternalWindows[i].y - 26,
                         prevExternalWindows[i].w + 4, prevExternalWindows[i].h + 30);
            }
        }

        // Cursor dirty (old + new)
        DirtyAdd(g_dirty, prev_mouse_x - CURSOR_PAD, prev_mouse_y - CURSOR_PAD, CURSOR_DIRTY_W, CURSOR_DIRTY_H);
        DirtyAdd(g_dirty, mouse_x - CURSOR_PAD, mouse_y - CURSOR_PAD, CURSOR_DIRTY_W, CURSOR_DIRTY_H);

        // Window moves (drag/max/min)
        for (int i = 0; i < window_count; i++) {
            if (windows[i].minimized != prev_min[i] || windows[i].maximized != prev_max[i]) {
                // Minimize/restore doesn't change geometry, but it changes visibility.
                DirtyAdd(g_dirty, prev_wx[i] - 2, prev_wy[i] - 2, prev_ww[i] + 4, prev_wh[i] + 4);
                DirtyAdd(g_dirty, windows[i].x - 2, windows[i].y - 2, windows[i].width + 4, windows[i].height + 4);
                ui_dirty = true;
            }
            if (windows[i].x != prev_wx[i] || windows[i].y != prev_wy[i] ||
                windows[i].width != prev_ww[i] || windows[i].height != prev_wh[i]) {
                DirtyAdd(g_dirty, prev_wx[i] - 2, prev_wy[i] - 2, prev_ww[i] + 4, prev_wh[i] + 4);
                DirtyAdd(g_dirty, windows[i].x - 2, windows[i].y - 2, windows[i].width + 4, windows[i].height + 4);
            }
        }

        // Taskbar/menu changes
        uint32_t now_sec = GetClockSeconds();
        if (now_sec != prev_clock_sec || menu_open != prev_menu_open) {
            int taskH = 40;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            // NOTE: Launcher is fullscreen, so when it changes state we mark full dirty
            // But this only triggers the full flush in Present(), not here
            // The flush decision is now in the Present() call
        }

        // If RTC second changed, update the taskbar clock.
        if (now_sec != g_last_clock_sec) {
            int taskH = 40;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            g_last_clock_sec = now_sec;
        }

        // Any UI reorder/state change: repaint taskbar only
        // Full screen dirty is now handled by PresentFullDirty() when launcher closes
        if (ui_dirty) {
            int taskH = 40;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
        }
        
        // C. Render
        // If nothing marked dirty (rare), still redraw cursor region.
        if (!g_dirty.valid) {
            DirtyAdd(g_dirty, mouse_x - CURSOR_PAD, mouse_y - CURSOR_PAD, CURSOR_DIRTY_W, CURSOR_DIRTY_H);
        }

        // Redraw only dirty region: background + UI + windows + cursor.
        // (Compositor is clip-aware internally.)
        // Set clip via SwapBuffersRect usage: we redraw full but clipped.
        // We reuse RenderScene for windows/cursor and draw taskbar/menu explicitly.
        if (needsFullFlush) {
            Compositor::SetClip(0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        } else {
            Compositor::SetClip(g_dirty.x, g_dirty.y, g_dirty.w, g_dirty.h);
        }
        // Full render within clip
        // Background is drawn inside RenderScene
        Compositor::RenderScene(windows, window_count, mouse_x, mouse_y);
        Compositor::RenderTaskbar(windows, window_count, 
                                externalWindows, externalWindowCount, 
                                menu_open, g_rtc);
        
        // D. Swap / Present Logic (Moved to end of frame)
        // NOTE: Removed sys_yield() here - it was causing unnecessary context switches
        // and introducing lag. The scheduler's timer-based preemption is sufficient.
        
        // System window content (text-based, lightweight) - HIDE if launcher open
        if (!menu_open) {
            for (int i = 0; i < window_count; i++) {
                Compositor::Window& w = windows[i];
                if (w.minimized) continue;
                if (!w.title) continue;
                // Match "System"
                if (w.title[0] == 'S') {
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

                    // Disk Info
                    char lineDisk[64];
                    pos = 0;
                    const char* d0 = "DISK: ";
                    while (*d0 && pos < 63) lineDisk[pos++] = *d0++;
                    
                    // Convert to GB if > 1GB, else MB
                    if (g_sys.disk_total_bytes >= 1024*1024*1024) {
                        uint64_t gb = g_sys.disk_total_bytes / (1024*1024*1024);
                        uint64_t rem = (g_sys.disk_total_bytes % (1024*1024*1024)) * 10 / (1024*1024*1024);
                        write_u64(lineDisk, pos, gb);
                        lineDisk[pos++] = '.';
                        write_u64(lineDisk, pos, rem);
                        const char* unit = " GB";
                        while (*unit && pos < 63) lineDisk[pos++] = *unit++;
                    } else {
                        uint64_t mb = g_sys.disk_total_bytes / (1024*1024);
                        write_u64(lineDisk, pos, mb);
                        const char* unit = " MB";
                        while (*unit && pos < 63) lineDisk[pos++] = *unit++;
                    }
                    
                    // Free space (Placeholder)
                    const char* d1 = " (RAW)";
                    while (*d1 && pos < 63) lineDisk[pos++] = *d1++;
                    
                    lineDisk[pos] = '\0';
                    UppercaseInplace(lineDisk);
                    Compositor::DrawText(x, y, lineDisk, 0xFFCCCCCC, 1);
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
                        y += 12;

                        // Frame Time (Phase 3 Requirement)
                        char lineFT[32];
                        pos = 0;
                        const char* ft = "FT: ";
                        while (*ft) lineFT[pos++] = *ft++;
                        write_u64(lineFT, pos, g_last_render_time);
                        lineFT[pos++] = 'm'; lineFT[pos++] = 's';
                        lineFT[pos] = 0;
                        Compositor::DrawText(x, y, lineFT, 0xFF88CC88, 1);
                        y += 12;

                        char lineEv[32];
                        pos = 0;
                        const char* evs = "EV: ";
                        while (*evs) lineEv[pos++] = *evs++;
                        write_u64(lineEv, pos, g_last_polled_events);
                        lineEv[pos] = 0;
                        Compositor::DrawText(x, y, lineEv, 0xFF88AAAA, 1);
                        y += 12;

                        char lineDrop[40];
                        pos = 0;
                        const char* dr = "DROP: ";
                        while (*dr) lineDrop[pos++] = *dr++;
                        write_u64(lineDrop, pos, g_last_input_drop_count);
                        lineDrop[pos] = 0;
                        uint32_t dropColor = (g_last_input_drop_count > 0) ? 0xFF4040C0 : 0xFF88CC88;
                        Compositor::DrawText(x, y, lineDrop, dropColor, 1);
                        y += 12;

                        char lineClients[40];
                        pos = 0;
                        const char* cl = "CLIENTS: ";
                        while (*cl) lineClients[pos++] = *cl++;
                        write_u64(lineClients, pos, g_registered_client_count);
                        lineClients[pos] = 0;
                        Compositor::DrawText(x, y, lineClients, 0xFF88AAAA, 1);
                        y += 12;

                        char lineClientPid[40];
                        pos = 0;
                        const char* cp = "LAST PID: ";
                        while (*cp && pos < 39) lineClientPid[pos++] = *cp++;
                        write_u64(lineClientPid, pos, g_last_client_pid);
                        lineClientPid[pos] = 0;
                        Compositor::DrawText(x, y, lineClientPid, 0xFF88AAAA, 1);
                        y += 12;

                        char lineSurf[40];
                        pos = 0;
                        const char* sf = "SURF: ";
                        while (*sf) lineSurf[pos++] = *sf++;
                        write_u64(lineSurf, pos, g_active_surface_count);
                        lineSurf[pos] = 0;
                        Compositor::DrawText(x, y, lineSurf, 0xFF88AAAA, 1);
                        y += 12;

                        char lineCommit[40];
                        pos = 0;
                        const char* sc = "SCOM: ";
                        while (*sc) lineCommit[pos++] = *sc++;
                        write_u64(lineCommit, pos, g_total_surface_commits);
                        lineCommit[pos] = 0;
                        Compositor::DrawText(x, y, lineCommit, 0xFF88CC88, 1);
                        y += 12;

                        char lineDe[32];
                        pos = 0;
                        const char* de = "DE: ";
                        while (*de) lineDe[pos++] = *de++;
                        write_u64(lineDe, pos, g_last_display_events);
                        lineDe[pos] = 0;
                        Compositor::DrawText(x, y, lineDe, 0xFF88AAAA, 1);
                        y += 12;

                        char lineFlip[40];
                        pos = 0;
                        const char* fl = "FLIP: ";
                        while (*fl) lineFlip[pos++] = *fl++;
                        write_u64(lineFlip, pos, g_last_flip_sequence);
                        lineFlip[pos] = 0;
                        Compositor::DrawText(x, y, lineFlip, 0xFF88CC88, 1);
                    }
                }
                // Match "MPK Installer"
                else if (w.title[0] == 'M') {
                    int x = w.x + 10;
                    int y = w.y + 40;
                    Compositor::DrawText(x, y, "MPK Installer", 0xFFFFFFFF, 2);
                    y += 30;
                    Compositor::DrawText(x, y, "Select .mpk file to install...", 0xFFCCCCCC, 1);
                    // Placeholder UI
                    Compositor::DrawRect(x, y + 20, 200, 30, 0xFF202020);
                    Compositor::DrawText(x + 10, y + 28, "Browse...", 0xFFAAAAAA, 1);
                }
            }
        }

        // Removed initial launcher draw. It is now Post-Compose.
        /* 
        if (menu_open) {
            g_launcher.Draw(Compositor::GetWidth(), Compositor::GetHeight());
        }
        */

        // Removed Pre-Compose Cursor drawing
        Compositor::ClearClip();

        // FINAL PIPELINE STEP:
        // 1. Flush Desktop Scratch -> Kernel RAM Buffer
        // This puts the Wallpaper + Icons + Taskbar into the shared buffer
        // Note: Default Target is BACK_BUFFER, used by RenderScene/RenderTaskbar
        
        // CRITICAL: Force full flush in any of these cases:
        // 1. Launcher just closed
        // 2. External app windows are active (prevents ghosting when apps move/close)
        if (needsFullFlush) {
            // Full screen copy ensures all areas under app windows are redrawn.
            Compositor::Flush();
        } else if (g_dirty.valid) {
            Compositor::FlushRect(g_dirty.x, g_dirty.y, g_dirty.w, g_dirty.h);
        } else {
             // Ensure at least cursor area update if nothing else
        }

        // [CRITICAL FIX] 1b. Clear Previous Cursor Trail
        // Before we ask Kernel to draw the NEW buffer (Compose), we must ensure 
        // the background buffer (Shared) is clean of the OLD cursor.
        // We copy the clean background from Scratch -> Shared at the OLD position.
        // We do this BEFORE sys_compose_layers to avoid overwriting the NEW cursor if they overlap.
        if (prev_mouse_x != mouse_x || prev_mouse_y != mouse_y) {
               Compositor::FlushRect(prev_mouse_x, prev_mouse_y, CURSOR_W, CURSOR_H);
        }

        // 2. Compose Apps (Kernel Overlay) 
        // Kernel draws apps ON TOP of the shared buffer
        sys_compose_layers();

           // 3. Post-Compose Overlay (Launcher + Cursor)
           // Draw overlays directly to the shared front buffer so they are above APP_WINDOW layers.
           Compositor::SetRenderTarget(Compositor::RenderTarget::FRONT_BUFFER);

           // 3a. Draw Launcher (if open) - On top of app windows.
        if (menu_open) {
             g_launcher.Draw(Compositor::GetWidth(), Compositor::GetHeight());
        }

              // 3a.1 Protocol surface badges over bound client windows.
              DrawProtocolSurfaceBadges();

           // 3b. Draw Cursor (userspace-owned)
           Compositor::DrawCursorToFront(mouse_x, mouse_y);
        
        // Measure Render Time (CPU) before VSync wait
        uint64_t time_render_done = sys_get_time_ms();
        g_last_render_time = time_render_done - start_time;

        // 4. Present via DRM.
        // Prefer the atomic path when available; keep legacy fallback during transition.
        bool vsynced = false;
        if (g_has_atomic_commit) {
            if (needsFullFlush || !g_dirty.valid) {
                vsynced = MorphicGfx::AtomicCommitFull(true) != 0;
            } else {
                vsynced = MorphicGfx::AtomicCommit((int16_t)g_dirty.x,
                                                   (int16_t)g_dirty.y,
                                                   (uint16_t)g_dirty.w,
                                                   (uint16_t)g_dirty.h,
                                                   MorphicGfx::ATOMIC_WAIT_VSYNC) != 0;
                if (!vsynced) {
                    vsynced = MorphicGfx::AtomicCommitFull(true) != 0;
                }
            }
        } else {
            if (needsFullFlush) {
                MorphicGfx::MarkCompositorDirty(0, 0,
                                                (uint16_t)Compositor::GetWidth(),
                                                (uint16_t)Compositor::GetHeight());
            } else if (g_dirty.valid) {
                MorphicGfx::MarkCompositorDirty((int16_t)g_dirty.x,
                                                (int16_t)g_dirty.y,
                                                (uint16_t)g_dirty.w,
                                                (uint16_t)g_dirty.h);
            }
            vsynced = MorphicGfx::Present(MorphicGfx::ATOMIC_WAIT_VSYNC) != 0;
        }

        if (g_has_atomic_commit) {
            PollDisplayEvents();
        }
        
        // Restore rendering to back buffer for next frame UI
        Compositor::SetRenderTarget(Compositor::RenderTarget::BACK_BUFFER);
        
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
        
        // Update previous external windows state for next frame comparison
        for (uint32_t i = 0; i < externalWindowCount && i < MAX_EXT_WINDOWS; i++) {
            prevExternalWindows[i] = externalWindows[i];
        }
        prevExternalWindowCount = externalWindowCount;
    }
    return 0;
}
