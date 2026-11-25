#include "os_event.h"
#include "compositor.h"
#include "system_info.h"
#include "launcher.h"
#include "morphic_syscalls.h"
#include "graphics_uapi.h"
#include "../../sdk/compositor_protocol.h"
#include "../../sdk/morphic_vulkan.h"

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

struct MouseProfile {
    const char* name;
    int sensitivity;
    int32_t max_step_x16;
    int32_t max_step_y16;
};

static constexpr MouseProfile k_mouse_profiles[] = {
    {"RAW 1:1", 1, 4096 << 16, 4096 << 16},
    {"DIRECT",  2, 4096 << 16, 4096 << 16},
    {"FAST",    3, 4096 << 16, 4096 << 16},
};
static constexpr int k_mouse_profile_count = (int)(sizeof(k_mouse_profiles) / sizeof(k_mouse_profiles[0]));
static int g_mouse_profile_index = 1; // DIRECT (zero-lag 1:1) default

static const MouseProfile& GetMouseProfile() {
    return k_mouse_profiles[g_mouse_profile_index];
}

static constexpr int CURSOR_W = 16;
static constexpr int CURSOR_H = 22;
static constexpr int CURSOR_PAD = 3;
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
static MorphicVkPhysicalDeviceInfo g_vk = {};
static bool g_vk_valid = false;
static GraphicsUapiCaps g_gfx_caps = {};
static bool g_gfx_caps_valid = false;
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
    uint32_t last_submit_serial;
    uint32_t last_presented_serial;
    bool in_flight;
    uint64_t commit_count;
    uint64_t last_commit_ms;
    uint64_t last_submit_ms;
    uint64_t overrun_count;
    uint64_t timeout_count;
};

static ProtocolSurface g_protocol_surfaces[MAX_PROTOCOL_CLIENTS] = {};
static uint32_t g_active_surface_count = 0;
static uint64_t g_total_surface_commits = 0;
static uint64_t g_total_surface_acks = 0;
static uint64_t g_total_surface_timeouts = 0;
static uint64_t g_total_surface_overruns = 0;

static Launcher g_launcher;

// Desktop Icon Grid + Desktop Right-Click Context Menu
struct DesktopIcon {
    char name[32];
    char mpk_path[64];
    uint32_t colorTop;
    uint32_t colorBot;
};
static constexpr int MAX_DESKTOP_ICONS = 16;
static DesktopIcon g_desktop_icons[MAX_DESKTOP_ICONS];
static int  g_desktop_icon_count = 0;
static bool g_show_desktop_icons = true;
static bool g_compact_icon_grid  = false;
static bool g_desk_ctx_open      = false;
static int  g_desk_ctx_x         = 0;
static int  g_desk_ctx_y         = 0;

static void AddDesktopIcon(const char* name, const char* path) {
    if (!name || !path || g_desktop_icon_count >= MAX_DESKTOP_ICONS) return;
    for (int i = 0; i < g_desktop_icon_count; i++) {
        const char* a = g_desktop_icons[i].mpk_path; const char* b = path;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b) return; // Already pinned
    }
    DesktopIcon& ic = g_desktop_icons[g_desktop_icon_count++];
    int p = 0; while (name[p] && p < 31) { ic.name[p] = name[p]; p++; } ic.name[p] = 0;
    p = 0; while (path[p] && p < 63) { ic.mpk_path[p] = path[p]; p++; } ic.mpk_path[p] = 0;
    if (name[0] == 'B') { ic.colorTop = 0xFF38BDF8; ic.colorBot = 0xFF0284C7; }
    else if (name[0] == 'I') { ic.colorTop = 0xFFF472B6; ic.colorBot = 0xFFDB2777; }
    else if (name[0] == 'T') { ic.colorTop = 0xFF818CF8; ic.colorBot = 0xFF4F46E5; }
    else if (name[0] == 'C') { ic.colorTop = 0xFF34D399; ic.colorBot = 0xFF059669; }
    else { ic.colorTop = 0xFFFBBF24; ic.colorBot = 0xFFD97706; }
}

static void InitDesktopIcons() {
    g_desktop_icon_count = 0;
    AddDesktopIcon("Browser",    "/initrd/browser.mpk");
    AddDesktopIcon("Installer",  "/initrd/installer.mpk");
    AddDesktopIcon("Terminal",   "/initrd/terminal.mpk");
    AddDesktopIcon("Calculator", "/initrd/calculator.mpk");
}

static void RenderDesktopIcons() {
    if (!g_show_desktop_icons || menu_open) return;
    int cellW = g_compact_icon_grid ? 76 : 92;
    int cellH = g_compact_icon_grid ? 82 : 98;
    int iconSz = g_compact_icon_grid ? 44 : 54;
    int startX = Compositor::GetWidth() - cellW - 22;
    int startY = 44;

    for (int i = 0; i < g_desktop_icon_count; i++) {
        int row = i % 6;
        int col = i / 6;
        int x = startX - col * (cellW + 10);
        int y = startY + row * (cellH + 10);

        Compositor::DrawRoundedRect(x, y, cellW, cellH, 12, 0x66111827);
        Compositor::DrawRoundedBorder(x, y, cellW, cellH, 12, 0x26FFFFFF);
        int ix = x + (cellW - iconSz) / 2;
        int iy = y + 8;
        Compositor::DrawRoundedGradientV(ix, iy, iconSz, iconSz, 14, g_desktop_icons[i].colorTop, g_desktop_icons[i].colorBot);
        char mono[3] = {g_desktop_icons[i].name[0], g_desktop_icons[i].name[1] ? g_desktop_icons[i].name[1] : '\0', '\0'};
        int mw = Compositor::MeasureText(mono, 2);
        Compositor::DrawText(ix + (iconSz - mw) / 2, iy + (iconSz - 32) / 2, mono, 0xFFFFFFFF, 2);
        int tw = Compositor::MeasureText(g_desktop_icons[i].name, 1);
        Compositor::DrawText(x + (cellW - tw) / 2, iy + iconSz + 6, g_desktop_icons[i].name, 0xFFE2E8F0, 1);
    }
}

static void RenderDesktopContextMenu() {
    if (!g_desk_ctx_open || menu_open) return;
    int cw = 204, ch = 196;
    int x = g_desk_ctx_x, y = g_desk_ctx_y;
    if (x + cw > Compositor::GetWidth() - 10) x = Compositor::GetWidth() - cw - 10;
    if (y + ch > Compositor::GetHeight() - 64) y = Compositor::GetHeight() - ch - 64;
    Compositor::DrawSoftShadow(x, y, cw, ch, 12, 10, 95);
    Compositor::DrawRoundedRect(x, y, cw, ch, 12, 0xF5151E32);
    Compositor::DrawRoundedBorder(x, y, cw, ch, 12, 0x4438BDF8);
    Compositor::DrawText(x + 14, y + 12,  g_show_desktop_icons ? "Hide Desktop Icons" : "Show Desktop Icons", 0xFFF1F5F9, 1);
    Compositor::DrawText(x + 14, y + 44,  g_compact_icon_grid  ? "Grid: Normal Size"  : "Grid: Compact Size", 0xFFF1F5F9, 1);
    Compositor::DrawText(x + 14, y + 76,  "Next Wallpaper Theme", 0xFF38BDF8, 1);
    Compositor::DrawText(x + 14, y + 108, "Cycle VSync (60-240Hz)", 0xFF34D399, 1);
    Compositor::DrawText(x + 14, y + 140, "Open Morphic Browser", 0xFFE2E8F0, 1);
    Compositor::DrawText(x + 14, y + 170, "Open Terminal",        0xFFE2E8F0, 1);
}

// Interaction State
int drag_target_idx = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;

void InitWindows() {
    windows[0] = {80, 76, 460, 344, 0xFF38BDF8, "System Monitor", false, false, false, 0,0,0,0};
    window_count = 1;
    InitDesktopIcons();
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

    if (g_last_drop_poll_ms == 0 || (now - g_last_drop_poll_ms) >= 500) {
        g_last_input_drop_count = sys_get_input_drop_count();
        g_vk_valid = MorphicVK::QueryPhysicalDevice(&g_vk);
        g_gfx_caps_valid = (MorphicGfx::QueryCaps(&g_gfx_caps) != 0);
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

void CycleMouseProfile() {
    g_mouse_profile_index = (g_mouse_profile_index + 1) % k_mouse_profile_count;
    const MouseProfile& profile = GetMouseProfile();
    sys_debug_print("Mouse profile: ");
    sys_debug_print(profile.name);
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

static void AckProtocolSurface(ProtocolSurface* surface, uint32_t flags) {
    if (!surface || !surface->active || surface->client_pid == 0 || !surface->in_flight) {
        return;
    }

    MorphicCompositor::PostFrameDone((uint32_t)surface->client_pid,
                                     (uint32_t)surface->client_pid,
                                     surface->last_submit_serial,
                                     flags);
    surface->last_presented_serial = surface->last_submit_serial;
    surface->in_flight = false;
    g_total_surface_acks++;
}

static void ProcessProtocolSyncAfterPresent(bool present_ok, uint64_t now_ms) {
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        ProtocolSurface& surface = g_protocol_surfaces[i];
        if (!surface.active || !surface.in_flight) {
            continue;
        }

        if (present_ok) {
            AckProtocolSurface(&surface, COMPOSITOR_FRAME_DONE_OK);
            continue;
        }

        if (surface.last_submit_ms != 0 && (now_ms - surface.last_submit_ms) > 1500) {
            surface.timeout_count++;
            g_total_surface_timeouts++;
            AckProtocolSurface(&surface, COMPOSITOR_FRAME_DONE_TIMEOUT_RECOVERY);
        }
    }
}

static void CleanupStaleProtocolSurfaces(uint64_t now_ms) {
    bool changed = false;
    for (int i = 0; i < MAX_PROTOCOL_CLIENTS; i++) {
        ProtocolSurface& surface = g_protocol_surfaces[i];
        if (!surface.active) {
            continue;
        }

        int ext_idx = (surface.bound_window_id != 0)
            ? FindExternalWindowIndexById(surface.bound_window_id)
            : -1;
        bool has_visible_window = (ext_idx >= 0 && externalWindows[(uint32_t)ext_idx].flags != 0);
        bool stale_commit = (surface.last_commit_ms != 0 && (now_ms - surface.last_commit_ms) > 1200);

        if (!has_visible_window && (ext_idx < 0 || stale_commit)) {
            surface = {};
            changed = true;
        }
    }

    if (changed) {
        RecountProtocolSurfaces();
    }
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
    if (menu_open) {
        return;
    }
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
        // Never draw orphaned badges for hidden, minimized, or closed windows
        if (w.flags == 0 || w.pid != surface.client_pid) {
            continue;
        }

        int bx = (int)w.x + (int)w.w - 108;
        int by = (int)w.y - 21;
        if (bx < (int)w.x + 80) bx = (int)w.x + 80;
        if (by < 2) by = 2;

        Compositor::DrawRoundedRect(bx, by, 34, 16, 8, 0x3334D399);
        Compositor::DrawRoundedBorder(bx, by, 34, 16, 8, 0x8834D399);
        Compositor::DrawText(bx + 5, by + 0, "IPC", 0xFF34D399, 1);
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

static void DebugPrintU64(uint64_t value) {
    char tmp[24];
    int count = 0;
    if (value == 0) {
        tmp[count++] = '0';
    }
    while (value && count < 23) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    char out[24];
    int pos = 0;
    while (count > 0 && pos < 23) {
        out[pos++] = tmp[--count];
    }
    out[pos] = 0;
    sys_debug_print(out);
}

static void LogProtocolSyncMetrics(uint64_t now_ms) {
    static uint64_t last_log_ms = 0;
    if (last_log_ms != 0 && (now_ms - last_log_ms) < 5000) {
        return;
    }
    last_log_ms = now_ms;

    sys_debug_print("[Desktop][Sync] SCOM=");
    DebugPrintU64(g_total_surface_commits);
    sys_debug_print(" SACK=");
    DebugPrintU64(g_total_surface_acks);
    sys_debug_print(" STO=");
    DebugPrintU64(g_total_surface_timeouts);
    sys_debug_print(" SOVR=");
    DebugPrintU64(g_total_surface_overruns);
    sys_debug_print(" SURF=");
    DebugPrintU64(g_active_surface_count);
    sys_debug_print("\n");
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
            if (surface->in_flight) {
                surface->overrun_count++;
                g_total_surface_overruns++;
            }
            uint32_t serial = (msg.arg1 > 0) ? (uint32_t)msg.arg1 : (uint32_t)(surface->commit_count + 1);
            surface->last_submit_serial = serial;
            surface->in_flight = true;
            surface->last_submit_ms = sys_get_time_ms();
            surface->commit_count++;
            surface->last_commit_ms = surface->last_submit_ms;
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
        if (menu_open) {
            if (ev.ascii == 27) { // ESC closes launcher
                menu_open = false;
                g_launcher.ResetSearch();
                ui_dirty = true;
            } else {
                int act = g_launcher.HandleKey(ev.ascii);
                if (act == 2) {
                    menu_open = false;
                }
                if (act > 0) ui_dirty = true;
            }
            return;
        }
        // Forward keyboard input to the focused app FIRST so letters like 'm'/'M'
        // always work inside Terminal and all .mpk applications.
        if (active_pid != 0) {
            OSEvent fwd = ev;
            sys_post_message(active_pid, &fwd);
            return;
        }
        // Bare desktop shortcut (only when no app window has keyboard focus)
        if (ev.ascii == 'm' || ev.ascii == 'M') {
            CycleMouseProfile();
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
        
        // Handle Right-Click (Context Menus for Launcher & Desktop)
        static bool prev_right_down = false;
        bool right_down = (ev.buttons & 2) != 0;
        if (right_down && !prev_right_down && (sys_get_time_ms() - g_startup_time_ms) >= 500) {
            prev_right_down = true;
            const int rcx = (int)(mouse_target_x16 >> 16);
            const int rcy = (int)(mouse_target_y16 >> 16);
            if (menu_open) {
                if (g_launcher.HandleRightClick(rcx, rcy) > 0) ui_dirty = true;
            } else {
                g_desk_ctx_open = true;
                g_desk_ctx_x = rcx;
                g_desk_ctx_y = rcy;
                ui_dirty = true;
                DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
            }
            return;
        }
        prev_right_down = right_down;

        // Only process left-click on button DOWN transition (not release)
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

        if ((sys_get_time_ms() - g_startup_time_ms) < 500) {
            return;
        }

        // PRIORITY 0: Open Desktop Context Menu
        if (g_desk_ctx_open && !menu_open) {
            int cw = 204, ch = 196;
            int cx = g_desk_ctx_x, cy = g_desk_ctx_y;
            if (cx + cw > Compositor::GetWidth() - 10) cx = Compositor::GetWidth() - cw - 10;
            if (cy + ch > Compositor::GetHeight() - 64) cy = Compositor::GetHeight() - ch - 64;
            g_desk_ctx_open = false;
            ui_dirty = true;
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
            if (HitRect(click_x, click_y, cx, cy, cw, ch)) {
                int row = (click_y - cy) / 32;
                if (row == 0) g_show_desktop_icons = !g_show_desktop_icons;
                else if (row == 1) g_compact_icon_grid = !g_compact_icon_grid;
                else if (row == 2) Compositor::SetWallpaperTheme(Compositor::GetWallpaperTheme() + 1);
                else if (row == 3) {
                    static const uint32_t kHz[] = {60, 120, 144, 240};
                    uint32_t cur = g_gfx_caps.refresh_hz ? g_gfx_caps.refresh_hz : 60;
                    uint32_t nxt = 120;
                    for (int k = 0; k < 4; k++) if (kHz[k] == cur) { nxt = kHz[(k + 1) % 4]; break; }
                    MorphicGfx::SetDisplayMode(nxt, GRAPHICS_VSYNC_ADAPTIVE);
                }
                else if (row == 4) sys_spawn("/initrd/browser.mpk");
                else if (row == 5) sys_spawn("/initrd/terminal.mpk");
                return;
            }
        }

        // ========================================
        // PRIORITY 1: Launcher (captures ALL clicks when open)
        // ========================================
        if (menu_open) {
            if (g_launcher.IsContextOpen()) {
                char pinName[32] = {}, pinPath[64] = {};
                int act = g_launcher.HandleContextAction(click_x, click_y, pinName, pinPath);
                if (act == 2) { menu_open = false; }
                else if (act == 3) { AddDesktopIcon(pinName, pinPath); }
                ui_dirty = true;
                g_launcher.spawnPending = false;
                return;
            }
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
        // PRIORITY 2: Floating Glass Dock
        // ========================================
        const int taskH = 54;
        const int dockY = Compositor::GetHeight() - 44 - 8;
        const int taskY = Compositor::GetHeight() - taskH;
        if (click_y >= taskY) {
            int width = Compositor::GetWidth();

            // Keymap Toggle (near clock pill)
            if (HitRect(click_x, click_y, width - 200, dockY + 6, 40, 32)) {
                CycleKeymap();
                return;
            }

            // Apps Launcher button (x=26..118)
            if (HitRect(click_x, click_y, 26, dockY + 6, 92, 32)) {
                uint64_t now_toggle_ms = sys_get_time_ms();
                if ((now_toggle_ms - g_last_menu_toggle_ms) < 160) {
                    return;
                }
                menu_open = !menu_open;
                g_last_menu_toggle_ms = now_toggle_ms;
                if (menu_open) {
                    g_menu_open_ms = now_toggle_ms;
                    g_launcher.ScanApps(); // Refresh installed .mpk packages dynamically
                    sys_debug_print("[Desktop] Launcher opened\n");
                }
                ui_dirty = true;
                active_pid = 0;
                return;
            }

            // Window pills (start at x=138, stride=40)
            int iconX = 138;
            for (int i = 0; i < window_count; i++) {
                if (HitRect(click_x, click_y, iconX, dockY + 6, 32, 32)) {
                    windows[i].minimized = !windows[i].minimized;
                    ui_dirty = true;
                    if (!windows[i].minimized) BringToFront(i);
                    return;
                }
                iconX += 40;
            }
            return; // Consumed by dock
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
                     
                     // Also hide the window immediately and detach its IPC badge/surface
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 0; 
                     sys_update_window(w.id, xy, wh_flags);
                     for (int s = 0; s < MAX_PROTOCOL_CLIENTS; s++) {
                         if (g_protocol_surfaces[s].active && g_protocol_surfaces[s].bound_window_id == w.id) {
                             g_protocol_surfaces[s] = {};
                             RecountProtocolSurfaces();
                             break;
                         }
                     }
                     ui_dirty = true;
                     return;
                 }
                 
                 // Minimize Button (Hide)
                 if (HitRect(click_x, click_y, bxMin, by, btnSize, btnSize)) {
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 0;
                     sys_update_window(w.id, xy, wh_flags);
                     return;
                 }

                 // 2. Hit Test Title Bar for Drag + Raise to Front (Z-Order)
                 if (click_x >= (int)w.x && click_x < (int)(w.x + w.w) &&
                     click_y >= (int)w.y - titleH && click_y < (int)(w.y)) {
                     
                     drag_target_external_id = w.id;
                     drag_offset_x = click_x - w.x;
                     drag_offset_y = click_y - w.y;
                     active_pid = w.pid;
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 3; // visible + RAISE
                     sys_update_window(w.id, xy, wh_flags);
                     DirtyAdd(g_dirty, w.x - 12, w.y - 34, w.w + 24, w.h + 48);
                     return;
                 }

                 // 3. Forward Client Area Clicks + Raise to Front (Z-Order)
                 if (click_x >= (int)w.x && click_x < (int)(w.x + w.w) &&
                     click_y >= (int)w.y && click_y < (int)(w.y + w.h)) {
                     
                     uint64_t xy = ((uint64_t)w.x << 32) | w.y;
                     uint64_t wh_flags = ((uint64_t)w.w << 40) | ((uint64_t)w.h << 16) | 3; // visible + RAISE
                     sys_update_window(w.id, xy, wh_flags);
                     DirtyAdd(g_dirty, w.x - 12, w.y - 34, w.w + 24, w.h + 48);

                     OSEvent ev;
                     ev.type = OSEvent::MOUSE_CLICK;
                     ev.dx = click_x - w.x;
                     ev.dy = click_y - w.y;
                     ev.buttons = 1;
                     sys_post_message(w.pid, &ev);
                     active_pid = w.pid;
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
            // Desktop Icon Grid click (launch pinned .mpk app)
            if (g_show_desktop_icons) {
                int cellW = g_compact_icon_grid ? 76 : 92;
                int cellH = g_compact_icon_grid ? 82 : 98;
                int startX = Compositor::GetWidth() - cellW - 22;
                int startY = 44;
                for (int i = 0; i < g_desktop_icon_count; i++) {
                    int row = i % 6;
                    int col = i / 6;
                    int ix = startX - col * (cellW + 10);
                    int iy = startY + row * (cellH + 10);
                    if (HitRect(click_x, click_y, ix, iy, cellW, cellH)) {
                        sys_spawn(g_desktop_icons[i].mpk_path);
                        return;
                    }
                }
            }
            active_pid = 0; // Clicked bare desktop: clear focused app
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
        const MouseProfile& profile = GetMouseProfile();
        mouse_target_x16 += (int32_t)(ev.dx * profile.sensitivity) << 16;
        mouse_target_y16 += (int32_t)(ev.dy * profile.sensitivity) << 16;

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
    // True real-time 1:1 hardware cursor synchronization (zero smoothing lag, zero velocity cap)
    mouse_x16 = mouse_target_x16;
    mouse_y16 = mouse_target_y16;

    mouse_x = (int)(mouse_x16 >> 16);
    mouse_y = (int)(mouse_y16 >> 16);

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
                    
                    // Mark OLD pos dirty with full chrome + shadow padding (Zero drag trails)
                    DirtyAdd(g_dirty, externalWindows[i].x - 12, externalWindows[i].y - 34, w + 24, h + 48);
                    
                    // Update local cache
                    externalWindows[i].x = targetX;
                    externalWindows[i].y = targetY;
                    
                    // Mark NEW pos dirty with full chrome + shadow padding
                    DirtyAdd(g_dirty, targetX - 12, targetY - 34, w + 24, h + 48);
                    
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
    // Keep a bounded drain to avoid long input stalls in a single frame.
    int count = 0;
    while (sys_get_event(&ev) && count < 192) {
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
    if (!Compositor::SetBackend(Compositor::BackendType::GPU_EXPERIMENTAL)) {
        (void)Compositor::SetBackend(Compositor::BackendType::SOFTWARE);
    }
    bool ok = Compositor::Initialize();
    if (!ok) {
        sys_debug_print("[Desktop] Compositor::Initialize FAILED\n");
        return -1;
    }
    sys_debug_print("[Desktop] Compositor::Initialize OK\n");
    sys_debug_print("[Desktop] Render backend: ");
    sys_debug_print(Compositor::GetBackendName());
    sys_debug_print("\n");

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

    if (sys_spawn("/initrd/desktop.mpk") > 0) {
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
        CleanupStaleProtocolSurfaces(start_time);
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
        bool externalJustClosed = (prevExternalWindowCount > 0 && !hasExternalWindows);
        bool menuStateChanged = (menu_open != prev_menu_open);
        // Dirty-rect fast path: only force full 1920x1080 flush on startup, launcher toggle, or window close.
        // Stationary external windows use dirty rects (<0.5ms/frame -> unlocks true 120/144/240 FPS!).
        bool needsFullFlush = first_frame || menuStateChanged || (!menu_open && externalJustClosed);

        // If we'll do a full flush, force full dirty so we don't copy stale backbuffer regions.
        if (needsFullFlush) {
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        }

        // Add External Windows to Dirty when they move, resize, or appear/disappear
        for(uint32_t i=0; i<externalWindowCount; i++) {
            WindowInfo& w = externalWindows[i];
            if (w.flags) {
                bool foundPrev = false;
                for (uint32_t j = 0; j < prevExternalWindowCount; j++) {
                    if (prevExternalWindows[j].id == w.id) {
                        foundPrev = true;
                        if (prevExternalWindows[j].x != w.x || prevExternalWindows[j].y != w.y ||
                            prevExternalWindows[j].w != w.w || prevExternalWindows[j].h != w.h ||
                            prevExternalWindows[j].flags != w.flags) {
                            DirtyAdd(g_dirty, prevExternalWindows[j].x - 12, prevExternalWindows[j].y - 34,
                                     prevExternalWindows[j].w + 24, prevExternalWindows[j].h + 48);
                            DirtyAdd(g_dirty, w.x - 12, w.y - 34, w.w + 24, w.h + 48);
                        }
                        break;
                    }
                }
                if (!foundPrev) {
                    DirtyAdd(g_dirty, w.x - 12, w.y - 34, w.w + 24, w.h + 48);
                }
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
                DirtyAdd(g_dirty, prevExternalWindows[i].x - 12, prevExternalWindows[i].y - 34,
                         prevExternalWindows[i].w + 24, prevExternalWindows[i].h + 48);
            }
        }

        // Cursor dirty (ONLY when mouse actually moved!)
        bool mouseMoved = (mouse_x != prev_mouse_x) || (mouse_y != prev_mouse_y);
        if (mouseMoved) {
            DirtyAdd(g_dirty, prev_mouse_x - CURSOR_PAD, prev_mouse_y - CURSOR_PAD, CURSOR_DIRTY_W, CURSOR_DIRTY_H);
            DirtyAdd(g_dirty, mouse_x - CURSOR_PAD, mouse_y - CURSOR_PAD, CURSOR_DIRTY_W, CURSOR_DIRTY_H);
        }

        // Window moves (drag/max/min) — include full 16px Glass Dark soft drop shadow margin!
        for (int i = 0; i < window_count; i++) {
            if (windows[i].minimized != prev_min[i] || windows[i].maximized != prev_max[i]) {
                DirtyAdd(g_dirty, prev_wx[i] - 20, prev_wy[i] - 20, prev_ww[i] + 40, prev_wh[i] + 44);
                DirtyAdd(g_dirty, windows[i].x - 20, windows[i].y - 20, windows[i].width + 40, windows[i].height + 44);
                ui_dirty = true;
            }
            if (windows[i].x != prev_wx[i] || windows[i].y != prev_wy[i] ||
                windows[i].width != prev_ww[i] || windows[i].height != prev_wh[i]) {
                DirtyAdd(g_dirty, prev_wx[i] - 20, prev_wy[i] - 20, prev_ww[i] + 40, prev_wh[i] + 44);
                DirtyAdd(g_dirty, windows[i].x - 20, windows[i].y - 20, windows[i].width + 40, windows[i].height + 44);
            }
        }

        // Taskbar/menu changes
        uint32_t now_sec = GetClockSeconds();
        if (now_sec != prev_clock_sec || menu_open != prev_menu_open) {
            int taskH = 56;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
        }

        // If RTC second changed, update the dock clock.
        if (now_sec != g_last_clock_sec) {
            int taskH = 56;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            g_last_clock_sec = now_sec;
        }

        // Any UI reorder/state change: repaint dock + top bar
        if (ui_dirty) {
            int taskH = 56;
            DirtyAdd(g_dirty, 0, Compositor::GetHeight() - taskH, Compositor::GetWidth(), taskH);
            DirtyAdd(g_dirty, 0, 0, Compositor::GetWidth(), 30);
        }
        
        // Dynamic frame budget from active kernel VSync target (60..240 Hz)
        uint32_t activeHz = (g_gfx_caps_valid && g_gfx_caps.refresh_hz >= 30) ? g_gfx_caps.refresh_hz : 60u;
        uint64_t activeBudgetMs = 1000ULL / activeHz;
        if (activeBudgetMs < 1) activeBudgetMs = 1;

        // Refresh System Monitor card smoothly on clock/telemetry tick so live FPS & Hz update immediately
        if (!menu_open && (now_sec != prev_clock_sec || ui_dirty) && window_count > 0 && !windows[0].minimized) {
            DirtyAdd(g_dirty, windows[0].x - 16, windows[0].y - 16, windows[0].width + 32, windows[0].height + 36);
        }

        // C. Render (skip redundant idle frames, wake at full active refresh rate for zero-lag input)
        bool frameChanged = first_frame || needsFullFlush || ui_dirty || g_dirty.valid;
        if (!frameChanged) {
            uint64_t end_idle = sys_get_time_ms();
            uint64_t elapsed_idle = end_idle - start_time;
            if (elapsed_idle < activeBudgetMs) {
                sys_sleep((uint32_t)(activeBudgetMs - elapsed_idle));
            }
            continue;
        }

        if (needsFullFlush) {
            Compositor::SetClip(0, 0, Compositor::GetWidth(), Compositor::GetHeight());
        } else {
            Compositor::SetClip(g_dirty.x, g_dirty.y, g_dirty.w, g_dirty.h);
        }

        if (menu_open) {
            // Render Launcher cleanly into BACK_BUFFER (only full redraw on open or UI update)
            if (menuStateChanged || first_frame || ui_dirty) {
                Compositor::SetClip(0, 0, Compositor::GetWidth(), Compositor::GetHeight());
                g_launcher.Draw(Compositor::GetWidth(), Compositor::GetHeight());
                Compositor::RenderTaskbar(windows, window_count,
                                          externalWindows, externalWindowCount,
                                          menu_open, g_rtc);
                needsFullFlush = true;
            } else if (now_sec != prev_clock_sec) {
                int taskH = 60;
                int dockTop = Compositor::GetHeight() - taskH;
                Compositor::SetClip(0, dockTop, Compositor::GetWidth(), taskH);
                // Clear dock strip cleanly before redrawing translucent glass dock + clock
                Compositor::DrawRect(0, dockTop, Compositor::GetWidth(), taskH, 0xFF0E162B);
                Compositor::RenderTaskbar(windows, window_count,
                                          externalWindows, externalWindowCount,
                                          menu_open, g_rtc);
            }
        } else {
            Compositor::RenderScene(windows, window_count, mouse_x, mouse_y);
            RenderDesktopIcons();
            Compositor::RenderTaskbar(windows, window_count,
                                    externalWindows, externalWindowCount,
                                    menu_open, g_rtc);
        }
        
        // D. Swap / Present Logic (Moved to end of frame)
        // NOTE: Removed sys_yield() here - it was causing unnecessary context switches
        // and introducing lag. The scheduler's timer-based preemption is sufficient.
        
        // System window content (text-based, lightweight) - HIDE if launcher open
        if (!menu_open) {
            for (int i = 0; i < window_count; i++) {
                Compositor::Window& w = windows[i];
                if (w.minimized) continue;
                if (!w.title) continue;
                // Match "System Monitor"
                if (w.title[0] == 'S') {
                    int pad = 16;
                    int titleH = 30;
                    int cardX = w.x + pad;
                    int cardW = w.width - (pad * 2);
                    int y = w.y + titleH + 14;

                    auto write_u64 = [](char* out, int& pos, uint64_t v) {
                        char tmp[24];
                        int n = 0;
                        if (v == 0) tmp[n++] = '0';
                        while (v && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
                        for (int k = n - 1; k >= 0; k--) out[pos++] = tmp[k];
                    };
                    auto append_str = [](char* out, int& pos, int maxL, const char* s) {
                        while (*s && pos < maxL - 1) out[pos++] = *s++;
                    };

                    // Card 1: Hardware & GPU Telemetry
                    Compositor::DrawRoundedRect(cardX, y, cardW, 134, 10, 0xFF172033);
                    Compositor::DrawRoundedBorder(cardX, y, cardW, 134, 10, 0x26FFFFFF);
                    int tx = cardX + 14;
                    int ty = y + 10;

                    Compositor::DrawText(tx, ty, "HARDWARE & GRAPHICS", 0xFF38BDF8, 1);
                    ty += 22;

                    char buf[64];
                    int pos = 0;
                    append_str(buf, pos, 64, "CPU: ");
                    for (int k = 0; k < 12 && g_sys.cpu_vendor[k] && pos < 63; k++) buf[pos++] = g_sys.cpu_vendor[k];
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFFE2E8F0, 1);
                    ty += 20;

                    uint64_t totalMiB = g_sys.total_mem_bytes / (1024ULL * 1024ULL);
                    uint64_t freeMiB  = g_sys.free_mem_bytes / (1024ULL * 1024ULL);
                    pos = 0;
                    append_str(buf, pos, 64, "RAM: ");
                    write_u64(buf, pos, totalMiB);
                    append_str(buf, pos, 64, " MB   Free: ");
                    write_u64(buf, pos, freeMiB);
                    append_str(buf, pos, 64, " MB");
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF94A3B8, 1);
                    ty += 20;

                    pos = 0;
                    uint32_t dispHz = (g_gfx_caps_valid && g_gfx_caps.refresh_hz) ? g_gfx_caps.refresh_hz : 60u;
                    uint32_t dispFps = g_gfx_caps_valid ? g_gfx_caps.measured_fps : 0u;
                    append_str(buf, pos, 64, "Display: ");
                    write_u64(buf, pos, g_sys.fb_width);
                    append_str(buf, pos, 64, "x");
                    write_u64(buf, pos, g_sys.fb_height);
                    append_str(buf, pos, 64, " @ ");
                    write_u64(buf, pos, dispHz);
                    append_str(buf, pos, 64, "Hz (");
                    write_u64(buf, pos, dispFps);
                    append_str(buf, pos, 64, " FPS)");
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF94A3B8, 1);
                    ty += 20;

                    pos = 0;
                    append_str(buf, pos, 64, "GPU: ");
                    if (g_vk_valid) {
                        append_str(buf, pos, 64, g_vk.vendor_name);
                        append_str(buf, pos, 64, " (MorphicVK 1.3)");
                    } else {
                        append_str(buf, pos, 64, "Framebuffer Software");
                    }
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF34D399, 1);

                    // Card 2: Compositor & Runtime Metrics
                    int y2 = y + 146;
                    Compositor::DrawRoundedRect(cardX, y2, cardW, 138, 10, 0xFF172033);
                    Compositor::DrawRoundedBorder(cardX, y2, cardW, 138, 10, 0x26FFFFFF);
                    ty = y2 + 10;

                    Compositor::DrawText(tx, ty, "COMPOSITOR & RUNTIME", 0xFF818CF8, 1);
                    ty += 22;

                    pos = 0;
                    append_str(buf, pos, 64, "Frame: ");
                    write_u64(buf, pos, g_last_render_time);
                    append_str(buf, pos, 64, "ms   Flips: ");
                    write_u64(buf, pos, g_last_flip_sequence);
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF34D399, 1);
                    ty += 20;

                    pos = 0;
                    append_str(buf, pos, 64, "Mouse: ");
                    append_str(buf, pos, 64, GetMouseProfile().name);
                    append_str(buf, pos, 64, " (M)   Drops: ");
                    write_u64(buf, pos, g_last_input_drop_count);
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF94A3B8, 1);
                    ty += 20;

                    pos = 0;
                    append_str(buf, pos, 64, "Clients: ");
                    write_u64(buf, pos, g_registered_client_count);
                    append_str(buf, pos, 64, "   Surfaces: ");
                    write_u64(buf, pos, g_active_surface_count);
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFFE2E8F0, 1);
                    ty += 20;

                    pos = 0;
                    append_str(buf, pos, 64, "IPC Sync: ");
                    write_u64(buf, pos, g_total_surface_commits);
                    append_str(buf, pos, 64, "/");
                    write_u64(buf, pos, g_total_surface_acks);
                    append_str(buf, pos, 64, "   Timeouts: ");
                    write_u64(buf, pos, g_total_surface_timeouts);
                    buf[pos] = '\0';
                    Compositor::DrawText(tx, ty, buf, 0xFF38BDF8, 1);
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
        // [CRITICAL FIX] 1b. Clear Previous Cursor Area with Padding
        // Ensures zero cursor pixel trails even near edges or stationary window overlaps
        if (!needsFullFlush) {
            Compositor::FlushRect(prev_mouse_x - CURSOR_PAD,
                                  prev_mouse_y - CURSOR_PAD,
                                  CURSOR_DIRTY_W,
                                  CURSOR_DIRTY_H);
        }

        // 2. Compose Apps (Kernel Overlay) — skipped while fullscreen Launcher is open
        if (!menu_open) {
            sys_compose_layers();
        }

        // 3. Post-Compose Overlay (Badges + Cursor)
        Compositor::SetRenderTarget(Compositor::RenderTarget::FRONT_BUFFER);

        if (!menu_open) {
            DrawProtocolSurfaceBadges();
            RenderDesktopContextMenu();
        }

        // 3b. Draw Cursor (userspace-owned, always on top)
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

        uint64_t end_present_ms = sys_get_time_ms();
        ProcessProtocolSyncAfterPresent(vsynced, end_present_ms);
        LogProtocolSyncMetrics(end_present_ms);
        
        // Restore rendering to back buffer for next frame UI
        Compositor::SetRenderTarget(Compositor::RenderTarget::BACK_BUFFER);
        
        // E. High-Refresh Frame Pacing:
        // When DRM VSync is active, WaitForRetrace() already phase-locks to 60..240 Hz in kernel.
        // Only apply a coarse sleep fallback if VSync did not run.
        if (!vsynced) {
            uint32_t targetHz = (g_gfx_caps_valid && g_gfx_caps.refresh_hz >= 30) ? g_gfx_caps.refresh_hz : 60u;
            uint64_t budgetMs = 1000ULL / targetHz;
            if (budgetMs < 1) budgetMs = 1;
            uint64_t end_time = sys_get_time_ms();
            uint64_t elapsed = end_time - start_time;
            if (elapsed < budgetMs) {
                sys_sleep((uint32_t)(budgetMs - elapsed));
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
