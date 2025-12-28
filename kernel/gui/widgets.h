#pragma once

// MorphicGUI Widget System
// C++ implementation with Lua-like API structure

#include <stdint.h>
#include <stddef.h>

#define GUI_MAX_WIDGETS 64
#define GUI_MAX_TEXT_LEN 128

// Colors (ARGB) - Modern GitHub Dark Theme
#define COLOR_TRANSPARENT  0x00000000
#define COLOR_BLACK        0xFF000000
#define COLOR_WHITE        0xFFFFFFFF

// Modern UI Colors (brighter for visibility)
#define COLOR_BG_DARK      0xFF1A1A2E  // Dark background
#define COLOR_BG_MEDIUM    0xFF16213E  // Medium background
#define COLOR_BG_LIGHT     0xFF0F3460  // Light background
#define COLOR_TASKBAR      0xFF1F4068  // Taskbar (blue tint)
#define COLOR_DESKTOP      0xFF1A1A2E  // Desktop background
#define COLOR_BORDER       0xFF4A5568  // Borders
#define COLOR_ACCENT       0xFF238636  // Green accent
#define COLOR_ACCENT_HOVER 0xFF2EA043  // Hover green
#define COLOR_BUTTON       0xFF2D3748  // Button background
#define COLOR_BUTTON_HOVER 0xFF4A5568  // Button hover
#define COLOR_TEXT         0xFFE2E8F0  // Light text
#define COLOR_TEXT_DIM     0xFFA0AEC0  // Dimmed text



// Widget types
enum class WidgetType {
    NONE,
    BUTTON,
    LABEL,
    PANEL,
    IMAGE
};

// Forward declarations
struct Widget;
typedef void (*ClickHandler)(Widget* widget);

// Base widget structure
struct Widget {
    WidgetType type;
    int32_t x, y;
    uint32_t w, h;
    bool visible;
    bool hover;
    bool pressed;
    uint32_t id;
    
    // Content
    char text[GUI_MAX_TEXT_LEN];
    uint32_t fg_color;
    uint32_t bg_color;
    
    // Callback
    ClickHandler onclick;
    void* userdata;
};

// Mouse state
struct MouseState {
    int32_t x, y;
    bool left_down;
    bool right_down;
    bool left_clicked;
    bool right_clicked;
};

// Desktop state
struct DesktopState {
    uint32_t screen_w;
    uint32_t screen_h;
    uint32_t taskbar_h;
    uint64_t uptime_sec;
    bool menu_open;
    bool needs_redraw;
};

namespace MorphicGUI {
    // Initialize the GUI system
    void Init(uint32_t screen_w, uint32_t screen_h);
    
    // Widget creation
    Widget* CreateButton(const char* text, int x, int y, uint32_t w, uint32_t h, ClickHandler onclick = nullptr);
    Widget* CreateLabel(const char* text, int x, int y, uint32_t color = COLOR_WHITE);
    Widget* CreatePanel(int x, int y, uint32_t w, uint32_t h, uint32_t color);
    
    // Widget management
    void DestroyWidget(Widget* widget);
    void SetVisible(Widget* widget, bool visible);
    void SetText(Widget* widget, const char* text);
    void SetPosition(Widget* widget, int x, int y);
    
    // Event processing
    void UpdateMouse(int32_t x, int32_t y, bool left, bool right);
    void ProcessEvents();
    
    // Rendering
    void Draw();
    void MarkDirty();
    
    // State
    MouseState* GetMouseState();
    DesktopState* GetDesktopState();
    
    // Utility
    bool PointInWidget(Widget* w, int x, int y);
    uint64_t GetUptime();
}
