#pragma once
#include <stdint.h>
#include <stddef.h>
#include "system_info.h"

extern "C" {
    void* sys_video_map();
    uint64_t sys_get_screen_info();
    uint64_t sys_alloc_backbuffer(uint64_t size);
    uint64_t sys_video_flip(void* backbuffer);
    uint64_t sys_video_flip_flags(void* backbuffer, uint64_t flags);
    uint64_t sys_video_flip_rect(void* backbuffer, uint64_t xy, uint64_t wh);
}

class Compositor {
public:
    enum class BackendType {
        SOFTWARE = 0,
        GPU_EXPERIMENTAL = 1,
    };

    static bool Initialize();
    static void Shutdown();
    static bool SetBackend(BackendType backend);
    static BackendType GetBackend();
    static const char* GetBackendName();
    static bool IsBackendAccelerated();

    enum class RenderTarget {
        BACK_BUFFER,
        FRONT_BUFFER
    };
    static void SetRenderTarget(RenderTarget target);

    // Buffer Management
    static void Clear(uint32_t color);
    static bool SwapBuffers();
    static bool SwapBuffersRect(int x, int y, int w, int h);

    static void Flush();
    static void FlushRect(int x, int y, int w, int h);
    static bool Present();
    static bool PresentFullDirty();

    // Drawing Primitives (Clip-Aware + Alpha Support)
    static void DrawRect(int x, int y, int w, int h, uint32_t color);
    static void DrawRectAlpha(int x, int y, int w, int h, uint32_t argb);
    static void DrawRoundedRect(int x, int y, int w, int h, int radius, uint32_t argb);
    static void DrawRoundedBorder(int x, int y, int w, int h, int radius, uint32_t argb);
    static void DrawGradientV(int x, int y, int w, int h, uint32_t topColor, uint32_t bottomColor);
    static void DrawRoundedGradientV(int x, int y, int w, int h, int radius, uint32_t topColor, uint32_t bottomColor);
    static void DrawSoftShadow(int x, int y, int w, int h, int radius, int spread = 12, uint8_t maxAlpha = 95);
    static void DrawCircle(int cx, int cy, int r, uint32_t argb);
    static void DrawCursor(int x, int y);
    static void DrawCursorToFront(int x, int y);

    static int GetWidth() { return width; }
    static int GetHeight() { return height; }

    struct Window {
        int x, y, width, height;
        uint32_t color;
        const char* title;
        bool is_dragging;
        bool minimized;
        bool maximized;
        int restore_x, restore_y, restore_w, restore_h;
    };

    static void RenderScene(Window* windows, int windowCount, int mouseX, int mouseY);
    static void RenderTaskbar(Window* windows, int windowCount,
                              void* extWindows, int extWindowCount,
                              bool menuOpen, const MorphicDateTime& dt);
    static void RenderMenu(bool menuOpen);

    static void DrawText(int x, int y, const char* text, uint32_t color, int scale = 1);
    static int MeasureText(const char* text, int scale = 1);

    static void SetClip(int x, int y, int w, int h);
    static void ClearClip();
    static void SetWallpaperTheme(int themeId);
    static int  GetWallpaperTheme();

private:
    static uint32_t* frontBuffer;
    static uint32_t* backBuffer;
    static uint32_t* wallpaperBuffer;
    static uint32_t* currentBuffer;
    static int width;
    static int height;
    static int bpp;

    static bool clipEnabled;
    static int clipX, clipY, clipW, clipH;
    static bool IntersectsClip(int x, int y, int w, int h);
    static void BuildWallpaper();
    static void BlitWallpaperClipped();
    static void BlendPixelClipped(int x, int y, uint32_t argb);
    static void FillRectClipped(int x, int y, int w, int h, uint32_t color);
    static void DrawRectClipped(int x, int y, int w, int h, uint32_t color);
    static void DrawCursorClipped(int x, int y);
    static void DrawClockText(int rightX, int y, uint32_t hh, uint32_t mm, uint32_t ss, uint32_t dd, uint32_t mo);
    static void DrawChar(int x, int y, char ch, uint32_t color, int scale);
};
