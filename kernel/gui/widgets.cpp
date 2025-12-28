// MorphicGUI Widget System Implementation
// Desktop GUI framework for Morphic OS

#include "widgets.h"
#include "../hal/video/graphics.h"
#include "../hal/video/early_term.h"
#include "../hal/video/font_renderer.h"
#include "../hal/input/mouse.h"
#include "../utils/std.h"
#include "../mm/user_heap.h"

namespace MorphicGUI {
    // Widget pool
    static Widget widgets[GUI_MAX_WIDGETS];
    static int widgetCount = 0;
    
    // State
    static MouseState mouseState = {0};
    static DesktopState desktopState = {0};
    static uint64_t tickCount = 0;
    
    // Rendering optimization
    static bool backgroundDrawn = false;
    static int32_t prevMouseX = -1;
    static int32_t prevMouseY = -1;
    
    void Init(uint32_t screen_w, uint32_t screen_h) {
        desktopState.screen_w = screen_w;
        desktopState.screen_h = screen_h;
        desktopState.taskbar_h = 32;
        desktopState.uptime_sec = 0;
        desktopState.menu_open = false;
        desktopState.needs_redraw = true;
        
        widgetCount = 0;
        for (int i = 0; i < GUI_MAX_WIDGETS; i++) {
            widgets[i].type = WidgetType::NONE;
        }
        
        kmemset(&mouseState, 0, sizeof(mouseState));
    }
    
    static Widget* AllocWidget() {
        for (int i = 0; i < GUI_MAX_WIDGETS; i++) {
            if (widgets[i].type == WidgetType::NONE) {
                widgets[i].id = i;
                widgetCount++;
                return &widgets[i];
            }
        }
        return nullptr;
    }
    
    Widget* CreateButton(const char* text, int x, int y, uint32_t w, uint32_t h, ClickHandler onclick) {
        Widget* btn = AllocWidget();
        if (!btn) return nullptr;
        
        btn->type = WidgetType::BUTTON;
        btn->x = x;
        btn->y = y;
        btn->w = w;
        btn->h = h;
        btn->visible = true;
        btn->hover = false;
        btn->pressed = false;
        btn->fg_color = COLOR_WHITE;
        btn->bg_color = COLOR_BUTTON;
        btn->onclick = onclick;
        
        int len = kstrlen(text);
        if (len >= GUI_MAX_TEXT_LEN) len = GUI_MAX_TEXT_LEN - 1;
        kmemcpy(btn->text, text, len);
        btn->text[len] = 0;
        
        return btn;
    }
    
    Widget* CreateLabel(const char* text, int x, int y, uint32_t color) {
        Widget* lbl = AllocWidget();
        if (!lbl) return nullptr;
        
        lbl->type = WidgetType::LABEL;
        lbl->x = x;
        lbl->y = y;
        lbl->w = 0;
        lbl->h = 16;
        lbl->visible = true;
        lbl->fg_color = color;
        lbl->bg_color = COLOR_TRANSPARENT;
        lbl->onclick = nullptr;
        
        int len = kstrlen(text);
        if (len >= GUI_MAX_TEXT_LEN) len = GUI_MAX_TEXT_LEN - 1;
        kmemcpy(lbl->text, text, len);
        lbl->text[len] = 0;
        
        return lbl;
    }
    
    Widget* CreatePanel(int x, int y, uint32_t w, uint32_t h, uint32_t color) {
        Widget* panel = AllocWidget();
        if (!panel) return nullptr;
        
        panel->type = WidgetType::PANEL;
        panel->x = x;
        panel->y = y;
        panel->w = w;
        panel->h = h;
        panel->visible = true;
        panel->bg_color = color;
        panel->onclick = nullptr;
        
        return panel;
    }
    
    void DestroyWidget(Widget* widget) {
        if (widget && widget->type != WidgetType::NONE) {
            widget->type = WidgetType::NONE;
            widgetCount--;
        }
    }
    
    void SetVisible(Widget* widget, bool visible) {
        if (widget) {
            widget->visible = visible;
            desktopState.needs_redraw = true;
        }
    }
    
    void SetText(Widget* widget, const char* text) {
        if (widget && text) {
            int len = kstrlen(text);
            if (len >= GUI_MAX_TEXT_LEN) len = GUI_MAX_TEXT_LEN - 1;
            kmemcpy(widget->text, text, len);
            widget->text[len] = 0;
            desktopState.needs_redraw = true;
        }
    }
    
    void SetPosition(Widget* widget, int x, int y) {
        if (widget) {
            widget->x = x;
            widget->y = y;
            desktopState.needs_redraw = true;
        }
    }
    
    bool PointInWidget(Widget* w, int x, int y) {
        return x >= w->x && x < (int)(w->x + w->w) &&
               y >= w->y && y < (int)(w->y + w->h);
    }
    
    void UpdateMouse(int32_t x, int32_t y, bool left, bool right) {
        bool prevLeft = mouseState.left_down;
        
        mouseState.x = x;
        mouseState.y = y;
        mouseState.left_down = left;
        mouseState.right_down = right;
        mouseState.left_clicked = (prevLeft && !left);  // Released
        mouseState.right_clicked = false;
    }
    
    void ProcessEvents() {
        // Update mouse from HAL
        int mx = Mouse::GetX();
        int my = Mouse::GetY();
        bool leftBtn = Mouse::IsLeftPressed();
        UpdateMouse(mx, my, leftBtn, false);
        
        // Update widget hover states
        for (int i = 0; i < GUI_MAX_WIDGETS; i++) {
            Widget* w = &widgets[i];
            if (w->type == WidgetType::NONE || !w->visible) continue;
            
            bool wasHover = w->hover;
            w->hover = PointInWidget(w, mx, my);
            
            if (wasHover != w->hover) {
                desktopState.needs_redraw = true;
            }
            
            // Check click
            if (mouseState.left_clicked && w->hover && w->onclick) {
                w->onclick(w);
                desktopState.needs_redraw = true;
            }
        }
        
        // Update uptime
        tickCount++;
        desktopState.uptime_sec = tickCount / 60;  // Approximate
    }
    
    void Draw() {
        int mx = mouseState.x;
        int my = mouseState.y;
        
        // Full redraw each frame (SIMD is fast enough)
        // This is simpler and avoids dirty rect tracking overhead
        
        // Clear screen
        Graphics::Clear(COLOR_DESKTOP);
        
        // Taskbar
        uint32_t taskbarY = desktopState.screen_h - desktopState.taskbar_h;
        Graphics::FillRect(0, taskbarY, desktopState.screen_w, desktopState.taskbar_h, COLOR_TASKBAR);
        Graphics::FillRect(0, taskbarY, desktopState.screen_w, 1, COLOR_BORDER);
        
        // Draw all widgets
        for (int i = 0; i < GUI_MAX_WIDGETS; i++) {
            Widget* w = &widgets[i];
            if (w->type == WidgetType::NONE || !w->visible) continue;
            
            switch (w->type) {
                case WidgetType::BUTTON: {
                    uint32_t bgColor = w->hover ? COLOR_BUTTON_HOVER : w->bg_color;
                    uint32_t borderColor = w->hover ? COLOR_ACCENT : COLOR_BORDER;
                    Graphics::FillRect(w->x, w->y, w->w, w->h, bgColor);
                    Graphics::FillRect(w->x, w->y, w->w, 1, borderColor);
                    Graphics::FillRect(w->x, w->y + w->h - 1, w->w, 1, borderColor);
                    Graphics::FillRect(w->x, w->y, 1, w->h, borderColor);
                    Graphics::FillRect(w->x + w->w - 1, w->y, 1, w->h, borderColor);
                    break;
                }
                case WidgetType::LABEL:
                    Graphics::FillRect(w->x, w->y, 45, 10, w->fg_color);
                    break;
                case WidgetType::PANEL:
                    Graphics::FillRect(w->x, w->y, w->w, w->h, w->bg_color);
                    break;
                default: break;
            }
        }
        
        // Cursor is now drawn by IRQ12 fast path (Mouse::OnInterrupt)
        // No manual cursor drawing needed here
        
        // Single SIMD Flip
        Graphics::Flip();
    }





    
    void MarkDirty() {
        desktopState.needs_redraw = true;
    }
    
    MouseState* GetMouseState() {
        return &mouseState;
    }
    
    DesktopState* GetDesktopState() {
        return &desktopState;
    }
    
    uint64_t GetUptime() {
        return desktopState.uptime_sec;
    }
}

