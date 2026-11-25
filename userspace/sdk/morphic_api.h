#pragma once
#include "morphic_syscalls.h"
#include "compositor_protocol.h"
#include "os_event.h"
#include <stdint.h>
#include <stddef.h>
#include "font8x16.h"

// Standard colors
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_BLACK 0xFF000000
#define COLOR_RED   0xFFFF0000
#define COLOR_GREEN 0xFF00FF00
#define COLOR_BLUE  0xFF0000FF

namespace MorphicAPI {

    class Helpers {
    public:
        static void* memset(void* dest, int c, size_t n) {
            uint8_t* p = (uint8_t*)dest;
            uint8_t byte = (uint8_t)c;
            if (n >= 8 && ((uintptr_t)p & 7) == 0) {
                uint64_t pattern = byte;
                pattern |= pattern << 8;
                pattern |= pattern << 16;
                pattern |= pattern << 32;
                uint64_t* p64 = (uint64_t*)p;
                size_t count64 = n / 8;
                for (size_t i = 0; i < count64; i++) p64[i] = pattern;
                p += count64 * 8;
                n -= count64 * 8;
            }
            while (n--) *p++ = byte;
            return dest;
        }

        static void memset32(uint32_t* dest, uint32_t value, size_t count) {
            size_t i = 0;
            for (; i + 4 <= count; i += 4) {
                dest[i] = value;
                dest[i + 1] = value;
                dest[i + 2] = value;
                dest[i + 3] = value;
            }
            for (; i < count; i++) dest[i] = value;
        }

        static void* memcpy(void* dest, const void* src, size_t n) {
            if (n >= 8 && ((uintptr_t)dest & 7) == 0 && ((uintptr_t)src & 7) == 0) {
                uint64_t* d64 = (uint64_t*)dest;
                const uint64_t* s64 = (const uint64_t*)src;
                size_t count64 = n / 8;
                for (size_t i = 0; i < count64; i++) d64[i] = s64[i];
                uint8_t* d = (uint8_t*)dest + count64 * 8;
                const uint8_t* s = (const uint8_t*)src + count64 * 8;
                n -= count64 * 8;
                while (n--) *d++ = *s++;
            } else {
                uint8_t* d = (uint8_t*)dest;
                const uint8_t* s = (const uint8_t*)src;
                while (n--) *d++ = *s++;
            }
            return dest;
        }

        static size_t strlen(const char* s) {
            if (!s) return 0;
            size_t len = 0;
            while (s[len]) len++;
            return len;
        }

        static int strcmp(const char* a, const char* b) {
            if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
            while (*a && *b && *a == *b) { a++; b++; }
            return (unsigned char)*a - (unsigned char)*b;
        }

        static void strcpy_s(char* dst, const char* src, size_t max_len) {
            if (!dst || max_len == 0) return;
            size_t i = 0;
            if (src) {
                while (src[i] && i + 1 < max_len) {
                    dst[i] = src[i];
                    i++;
                }
            }
            dst[i] = '\0';
        }
    };

    namespace Color {
        static inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }

        static inline uint32_t WithAlpha(uint32_t argb, uint8_t a) {
            return (argb & 0x00FFFFFFu) | ((uint32_t)a << 24);
        }

        // Fast integer Porter-Duff source-over alpha blend (ARGB8888)
        static inline uint32_t Blend(uint32_t dst, uint32_t src) {
            uint32_t a = (src >> 24) & 0xFFu;
            if (a == 255u) return src;
            if (a == 0u) return dst;
            uint32_t inv = 255u - a;
            uint32_t rb = (((src & 0x00FF00FFu) * a) + ((dst & 0x00FF00FFu) * inv) + 0x00800080u);
            rb = (rb + ((rb >> 8) & 0x00FF00FFu)) >> 8;
            uint32_t g = ((((src >> 8) & 0xFFu) * a) + (((dst >> 8) & 0xFFu) * inv) + 128u);
            g = (g + (g >> 8)) >> 8;
            return 0xFF000000u | (rb & 0x00FF00FFu) | ((g & 0xFFu) << 8);
        }

        // Linear interpolation t in [0..255]
        static inline uint32_t Lerp(uint32_t c0, uint32_t c1, uint32_t t) {
            if (t == 0u) return c0;
            if (t >= 255u) return c1;
            uint32_t inv = 255u - t;
            uint32_t a = ((((c0 >> 24) & 0xFFu) * inv) + (((c1 >> 24) & 0xFFu) * t)) / 255u;
            uint32_t r = ((((c0 >> 16) & 0xFFu) * inv) + (((c1 >> 16) & 0xFFu) * t)) / 255u;
            uint32_t g = ((((c0 >> 8) & 0xFFu) * inv) + (((c1 >> 8) & 0xFFu) * t)) / 255u;
            uint32_t b = (((c0 & 0xFFu) * inv) + ((c1 & 0xFFu) * t)) / 255u;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    class Graphics {
    protected:
        uint32_t* fb;
        uint64_t width, height, pitch;

        static inline int iabs(int v) { return v < 0 ? -v : v; }

        // Integer sqrt for smooth corner coverage (no FPU required)
        static inline int isqrt_u32(uint32_t n) {
            uint32_t x = n;
            uint32_t y = (x + 1u) >> 1;
            while (y < x) {
                x = y;
                y = (x + n / x) >> 1;
            }
            return (int)x;
        }

    public:
        Graphics(uint32_t* framebuffer, uint64_t w, uint64_t h, uint64_t p)
            : fb(framebuffer), width(w), height(h), pitch(p) {}

        uint64_t GetWidth() const { return width; }
        uint64_t GetHeight() const { return height; }
        uint32_t* GetBuffer() const { return fb; }

        void Clear(uint32_t color) {
            Helpers::memset32(fb, color, width * height);
        }

        void PutPixel(int x, int y, uint32_t color) {
            if (x < 0 || y < 0 || (uint64_t)x >= width || (uint64_t)y >= height) return;
            fb[y * pitch + x] = color;
        }

        void BlendPixel(int x, int y, uint32_t color) {
            if (x < 0 || y < 0 || (uint64_t)x >= width || (uint64_t)y >= height) return;
            uint32_t& dst = fb[y * pitch + x];
            dst = Color::Blend(dst, color);
        }

        void FillRect(int x, int y, int w, int h, uint32_t color) {
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > (int)width) w = (int)width - x;
            if (y + h > (int)height) h = (int)height - y;
            if (w <= 0 || h <= 0) return;

            uint32_t a = (color >> 24) & 0xFFu;
            if (a == 255u) {
                for (int j = 0; j < h; j++) {
                    Helpers::memset32(fb + (y + j) * pitch + x, color, (size_t)w);
                }
            } else if (a > 0u) {
                for (int j = 0; j < h; j++) {
                    uint32_t* row = fb + (y + j) * pitch + x;
                    for (int i = 0; i < w; i++) {
                        row[i] = Color::Blend(row[i], color);
                    }
                }
            }
        }

        void DrawRect(int x, int y, int w, int h, uint32_t color, int thickness = 1) {
            if (w <= 0 || h <= 0 || thickness <= 0) return;
            FillRect(x, y, w, thickness, color);
            FillRect(x, y + h - thickness, w, thickness, color);
            FillRect(x, y + thickness, thickness, h - 2 * thickness, color);
            FillRect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
        }

        void DrawHLine(int x, int y, int w, uint32_t color) {
            FillRect(x, y, w, 1, color);
        }

        void DrawVLine(int x, int y, int h, uint32_t color) {
            FillRect(x, y, 1, h, color);
        }

        void DrawLine(int x0, int y0, int x1, int y1, uint32_t color) {
            int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            while (true) {
                BlendPixel(x0, y0, color);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        }

        void FillGradientV(int x, int y, int w, int h, uint32_t topColor, uint32_t bottomColor) {
            if (w <= 0 || h <= 0) return;
            int denom = h > 1 ? (h - 1) : 1;
            for (int j = 0; j < h; j++) {
                uint32_t t = (uint32_t)((j * 255) / denom);
                uint32_t col = Color::Lerp(topColor, bottomColor, t);
                FillRect(x, y + j, w, 1, col);
            }
        }

        void FillGradientH(int x, int y, int w, int h, uint32_t leftColor, uint32_t rightColor) {
            if (w <= 0 || h <= 0) return;
            int denom = w > 1 ? (w - 1) : 1;
            for (int i = 0; i < w; i++) {
                uint32_t t = (uint32_t)((i * 255) / denom);
                uint32_t col = Color::Lerp(leftColor, rightColor, t);
                FillRect(x + i, y, 1, h, col);
            }
        }

        // Anti-aliased rounded rectangle fill (supports translucent alpha)
        void FillRoundedRect(int x, int y, int w, int h, int radius, uint32_t color) {
            if (w <= 0 || h <= 0) return;
            if (radius <= 0) { FillRect(x, y, w, h, color); return; }
            int maxR = (w < h ? w : h) / 2;
            if (radius > maxR) radius = maxR;

            uint32_t baseAlpha = (color >> 24) & 0xFFu;
            if (baseAlpha == 0u) return;

            // Center horizontal band (fast bulk fill)
            if (h > 2 * radius) {
                FillRect(x, y + radius, w, h - 2 * radius, color);
            }

            // Top and bottom rounded bands (4x supersampled corner coverage)
            int r2 = radius * radius * 16;
            for (int row = 0; row < radius; row++) {
                int yTop = y + row;
                int yBot = y + h - 1 - row;

                // Middle straight span between left & right corners
                if (w > 2 * radius) {
                    FillRect(x + radius, yTop, w - 2 * radius, 1, color);
                    FillRect(x + radius, yBot, w - 2 * radius, 1, color);
                }

                // Corner pixels
                for (int col = 0; col < radius; col++) {
                    int covered = 0;
                    for (int sy = 0; sy < 4; sy++) {
                        int dy = (radius - row) * 4 - sy - 1;
                        for (int sx = 0; sx < 4; sx++) {
                            int dx = (radius - col) * 4 - sx - 1;
                            if (dx * dx + dy * dy <= r2) covered++;
                        }
                    }
                    if (covered == 0) continue;
                    uint32_t a = (baseAlpha * (uint32_t)covered) >> 4;
                    uint32_t pxColor = Color::WithAlpha(color, (uint8_t)a);
                    BlendPixel(x + col, yTop, pxColor);
                    BlendPixel(x + w - 1 - col, yTop, pxColor);
                    BlendPixel(x + col, yBot, pxColor);
                    BlendPixel(x + w - 1 - col, yBot, pxColor);
                }
            }
        }

        // Anti-aliased rounded rectangle with vertical gradient
        void FillRoundedGradientV(int x, int y, int w, int h, int radius, uint32_t topColor, uint32_t bottomColor) {
            if (w <= 0 || h <= 0) return;
            if (radius <= 0) { FillGradientV(x, y, w, h, topColor, bottomColor); return; }
            int maxR = (w < h ? w : h) / 2;
            if (radius > maxR) radius = maxR;

            int denom = h > 1 ? (h - 1) : 1;
            int r2 = radius * radius * 16;

            for (int j = 0; j < h; j++) {
                uint32_t t = (uint32_t)((j * 255) / denom);
                uint32_t rowColor = Color::Lerp(topColor, bottomColor, t);
                uint32_t baseAlpha = (rowColor >> 24) & 0xFFu;
                if (baseAlpha == 0u) continue;

                int rowInCorner = -1;
                if (j < radius) rowInCorner = j;
                else if (j >= h - radius) rowInCorner = h - 1 - j;

                if (rowInCorner < 0) {
                    FillRect(x, y + j, w, 1, rowColor);
                } else {
                    if (w > 2 * radius) {
                        FillRect(x + radius, y + j, w - 2 * radius, 1, rowColor);
                    }
                    for (int col = 0; col < radius; col++) {
                        int covered = 0;
                        for (int sy = 0; sy < 4; sy++) {
                            int dy = (radius - rowInCorner) * 4 - sy - 1;
                            for (int sx = 0; sx < 4; sx++) {
                                int dx = (radius - col) * 4 - sx - 1;
                                if (dx * dx + dy * dy <= r2) covered++;
                            }
                        }
                        if (covered == 0) continue;
                        uint32_t a = (baseAlpha * (uint32_t)covered) >> 4;
                        uint32_t pxColor = Color::WithAlpha(rowColor, (uint8_t)a);
                        BlendPixel(x + col, y + j, pxColor);
                        BlendPixel(x + w - 1 - col, y + j, pxColor);
                    }
                }
            }
        }

        // Crisp 1px anti-aliased rounded border (ideal for glass rim highlights)
        void DrawRoundedRect(int x, int y, int w, int h, int radius, uint32_t color) {
            if (w <= 1 || h <= 1) return;
            if (radius <= 0) { DrawRect(x, y, w, h, color, 1); return; }
            int maxR = (w < h ? w : h) / 2;
            if (radius > maxR) radius = maxR;

            FillRect(x + radius, y, w - 2 * radius, 1, color);
            FillRect(x + radius, y + h - 1, w - 2 * radius, 1, color);
            FillRect(x, y + radius, 1, h - 2 * radius, color);
            FillRect(x + w - 1, y + radius, 1, h - 2 * radius, color);

            uint32_t baseAlpha = (color >> 24) & 0xFFu;
            int rOuter2 = radius * radius * 16;
            int rInner = radius > 1 ? (radius - 1) : 0;
            int rInner2 = rInner * rInner * 16;

            for (int row = 0; row < radius; row++) {
                for (int col = 0; col < radius; col++) {
                    int ring = 0;
                    for (int sy = 0; sy < 4; sy++) {
                        int dy = (radius - row) * 4 - sy - 1;
                        for (int sx = 0; sx < 4; sx++) {
                            int dx = (radius - col) * 4 - sx - 1;
                            int d2 = dx * dx + dy * dy;
                            if (d2 <= rOuter2 && d2 >= rInner2) ring++;
                        }
                    }
                    if (ring == 0) continue;
                    uint32_t a = (baseAlpha * (uint32_t)ring) >> 4;
                    uint32_t pxColor = Color::WithAlpha(color, (uint8_t)a);
                    BlendPixel(x + col, y + row, pxColor);
                    BlendPixel(x + w - 1 - col, y + row, pxColor);
                    BlendPixel(x + col, y + h - 1 - row, pxColor);
                    BlendPixel(x + w - 1 - col, y + h - 1 - row, pxColor);
                }
            }
        }

        // Soft multi-layer ambient drop shadow
        void DrawShadow(int x, int y, int w, int h, int radius, int spread = 10, uint8_t maxAlpha = 90) {
            if (spread <= 0 || maxAlpha == 0) return;
            int layers = spread > 6 ? 6 : spread;
            for (int i = layers; i >= 1; i--) {
                int pad = (i * spread) / layers;
                uint8_t a = (uint8_t)((maxAlpha * (layers - i + 1)) / (layers * 2));
                FillRoundedRect(x - pad / 2, y + pad / 2, w + pad, h + pad, radius + pad / 2, Color::RGBA(0, 0, 0, a));
            }
        }

        // Anti-aliased filled circle
        void FillCircle(int cx, int cy, int r, uint32_t color) {
            if (r <= 0) return;
            FillRoundedRect(cx - r, cy - r, r * 2, r * 2, r, color);
        }

        static inline int MeasureTextWidth(const char* text, int scale = 1) {
            if (!text) return 0;
            int len = (int)Helpers::strlen(text);
            return len > 0 ? (len * 9 * scale - scale) : 0;
        }

        // VGA 8x16 Font Rendering - Optimized with Alpha Support
        void DrawChar(int x, int y, char c, uint32_t color, int scale = 1) {
            unsigned char uc = (unsigned char)c;
            if (uc > 0x7F) uc = '?';

            const unsigned char* glyph = &font8x16[uc * 16];
            uint32_t a = (color >> 24) & 0xFFu;

            if (scale == 1) {
                if (x < 0 || y < 0 || (uint64_t)(x + 8) > width || (uint64_t)(y + 16) > height || a < 255u) {
                    for (int r = 0; r < 16; r++) {
                        if (y + r < 0 || (uint64_t)(y + r) >= height) continue;
                        unsigned char row_bits = glyph[r];
                        for (int col = 0; col < 8; col++) {
                            if (row_bits & (0x80 >> col)) BlendPixel(x + col, y + r, color);
                        }
                    }
                    return;
                }
                for (int r = 0; r < 16; r++) {
                    unsigned char row_bits = glyph[r];
                    if (!row_bits) continue;
                    uint32_t* row_ptr = fb + (y + r) * pitch + x;
                    if (row_bits & 0x80) row_ptr[0] = color;
                    if (row_bits & 0x40) row_ptr[1] = color;
                    if (row_bits & 0x20) row_ptr[2] = color;
                    if (row_bits & 0x10) row_ptr[3] = color;
                    if (row_bits & 0x08) row_ptr[4] = color;
                    if (row_bits & 0x04) row_ptr[5] = color;
                    if (row_bits & 0x02) row_ptr[6] = color;
                    if (row_bits & 0x01) row_ptr[7] = color;
                }
            } else {
                for (int r = 0; r < 16; r++) {
                    unsigned char row_bits = glyph[r];
                    unsigned char prev_bits = (r > 0) ? glyph[r - 1] : 0;
                    unsigned char next_bits = (r < 15) ? glyph[r + 1] : 0;
                    for (int col = 0; col < 8; col++) {
                        bool on = (row_bits & (0x80 >> col)) != 0;
                        if (on) {
                            FillRect(x + col * scale, y + r * scale, scale, scale, color);
                        } else if (scale == 2) {
                            bool up    = (prev_bits & (0x80 >> col)) != 0;
                            bool down  = (next_bits & (0x80 >> col)) != 0;
                            bool left  = (col > 0) && ((row_bits & (0x80 >> (col - 1))) != 0);
                            bool right = (col < 7) && ((row_bits & (0x80 >> (col + 1))) != 0);
                            uint32_t aa = Color::WithAlpha(color, 0x58);
                            int bx = x + col * 2, by = y + r * 2;
                            if (up && left)    BlendPixel(bx,     by,     aa);
                            if (up && right)   BlendPixel(bx + 1, by,     aa);
                            if (down && left)  BlendPixel(bx,     by + 1, aa);
                            if (down && right) BlendPixel(bx + 1, by + 1, aa);
                        }
                    }
                }
            }
        }

        void DrawText(int x, int y, const char* text, uint32_t color, int scale = 1) {
            if (!text) return;
            int cx = x;
            while (*text) {
                if (*text == '\n') {
                    y += 18 * scale;
                    cx = x;
                } else {
                    DrawChar(cx, y, *text, color, scale);
                    cx += 9 * scale;
                }
                text++;
            }
        }

        void DrawTextCentered(int x, int y, int w, int h, const char* text, uint32_t color, int scale = 1) {
            int tw = MeasureTextWidth(text, scale);
            int th = 16 * scale;
            DrawText(x + (w - tw) / 2, y + (h - th) / 2, text, color, scale);
        }
    };


    class Window {
    protected:
        uint32_t* backbuffer;
        uint64_t width, height;
        uint64_t sys_width, sys_height;
        uint32_t requestW, requestH;
        bool running;
        int mouseX, mouseY;
        uint64_t appPid;
        uint64_t compositorPid;
        uint64_t lastProtocolHelloMs;
        bool protocolClientMode;
        bool protocolSurfaceAnnounced;
        uint32_t protocolSubmitSerial;
        uint32_t protocolAckSerial;
        uint64_t protocolLastCommitMs;
        uint64_t protocolTimeoutRecoveries;
        uint64_t protocolThrottleSleeps;

    public:
        Window(uint32_t w = 0, uint32_t h = 0)
            : backbuffer(nullptr), width(0), height(0),
              requestW(w), requestH(h), running(false),
              mouseX(-1), mouseY(-1),
              appPid(0), compositorPid(0), lastProtocolHelloMs(0),
              protocolClientMode(false), protocolSurfaceAnnounced(false),
              protocolSubmitSerial(0), protocolAckSerial(0),
              protocolLastCommitMs(0), protocolTimeoutRecoveries(0),
              protocolThrottleSleeps(0), kernelBuffer(nullptr), needsRedraw(true) {}
        virtual ~Window() {}

        uint32_t* kernelBuffer;

        int GetMouseX() const { return mouseX; }
        int GetMouseY() const { return mouseY; }
        uint64_t GetWidth() const { return width; }
        uint64_t GetHeight() const { return height; }
        uint32_t* GetBackbuffer() const { return backbuffer; }

        virtual bool Init() {
            const uint64_t MAX_PIXELS = 3840 * 2160;
            const uint64_t BUF_SIZE = MAX_PIXELS * 4;

            static uint32_t* s_scratchBuffer = nullptr;
            if (!s_scratchBuffer) {
                uint64_t addr = sys_alloc_backbuffer(BUF_SIZE);
                s_scratchBuffer = (uint32_t*)addr;
            }

            uint64_t info = sys_get_screen_info();
            sys_width = (info >> 32) & 0xFFFFFFFF;
            sys_height = info & 0xFFFFFFFF;
            if (sys_width == 0) sys_width = 1280;
            if (sys_height == 0) sys_height = 800;

            void* addr = nullptr;

            if (requestW > 0 && requestH > 0) {
                addr = sys_create_window(requestW, requestH, 0);
                width = requestW;
                height = requestH;
            } else {
                addr = sys_video_map();
                width = sys_width;
                height = sys_height;
            }

            if (!addr) return false;

            kernelBuffer = (uint32_t*)addr;

            if (s_scratchBuffer && (width * height <= MAX_PIXELS)) {
                backbuffer = s_scratchBuffer;
            } else {
                backbuffer = kernelBuffer;
            }

            running = true;
            appPid = sys_get_pid();
            SyncCompositorRegistration(true);
            return true;
        }

        virtual void OnUpdate() = 0;
        virtual void OnRender(Graphics& g) = 0;

        bool needsRedraw;

        void Invalidate() {
            needsRedraw = true;
        }

        void Close() {
            running = false;
        }

        void Run() {
            if (!Init()) {
                sys_debug_print("Failed to init window\n");
                return;
            }

            Graphics g(backbuffer, width, height, width);

            needsRedraw = true;

            uint32_t idleCounter = 0;
            const uint32_t MAX_IDLE_SLEEP = 100;
            const uint32_t MIN_IDLE_SLEEP = 16;

            while (running) {
                SyncCompositorRegistration(false);

                OSEvent ev;
                bool hadEvent = false;
                int maxEvents = 20;
                while (sys_get_event(&ev) && maxEvents--) {
                    hadEvent = true;

                    if (ev.type == OSEvent::USER_MESSAGE) {
                        CompositorMessage msg = {};
                        if (MorphicCompositor::DecodeMessage(ev, &msg) &&
                            msg.type == COMPOSITOR_MSG_FRAME_DONE &&
                            (uint32_t)msg.arg0 == (uint32_t)appPid) {
                            uint32_t ackSerial = (uint32_t)msg.arg1;
                            if (ackSerial > protocolAckSerial) {
                                protocolAckSerial = ackSerial;
                            }
                            if ((uint32_t)msg.arg3 == COMPOSITOR_FRAME_DONE_TIMEOUT_RECOVERY) {
                                protocolTimeoutRecoveries++;
                            }
                            OnCompositorFrameDone(ackSerial, (uint32_t)msg.arg3);
                            continue;
                        }
                    }

                    idleCounter = 0;

                    if (ev.type == OSEvent::WINDOW_DESTROYED) {
                        running = false;
                        break;
                    }

                    if (ev.type == OSEvent::KEY_PRESS) {
                        needsRedraw = true;
                        if (ev.ascii == 27) {
                            running = false;
                        }
                        OnKeyDown(ev.ascii);
                    }
                    if (ev.type == OSEvent::MOUSE_CLICK) {
                        mouseX = ev.dx;
                        mouseY = ev.dy;
                        needsRedraw = true;
                        OnMouseDown(ev.dx, ev.dy, ev.buttons);
                    }
                    if (ev.type == OSEvent::MOUSE_MOVE) {
                        mouseX = ev.dx;
                        mouseY = ev.dy;
                        OnMouseMove(ev.dx, ev.dy);
                    }
                }
                (void)hadEvent;

                OnUpdate();

                if (needsRedraw) {
                    uint64_t now = sys_get_time_ms();
                    if (ShouldThrottleCompositorCommit(now)) {
                        protocolThrottleSleeps++;
                        sys_sleep(1);
                        continue;
                    }

                    OnRender(g);

                    if (backbuffer != kernelBuffer) {
                        uint64_t* src = (uint64_t*)backbuffer;
                        uint64_t* dst = (uint64_t*)kernelBuffer;
                        size_t count = (width * height * 4) / 8;
                        for (size_t i = 0; i < count; i++) dst[i] = src[i];
                    }

                    sys_video_flip(kernelBuffer);
                    NotifyCompositorCommit();
                    needsRedraw = false;
                    idleCounter = 0;
                } else {
                    idleCounter++;
                    uint32_t sleepTime = MIN_IDLE_SLEEP + (idleCounter / 4);
                    if (sleepTime > MAX_IDLE_SLEEP) sleepTime = MAX_IDLE_SLEEP;
                    sys_sleep(sleepTime);
                }
            }
        }

        virtual void OnKeyDown(char c) { (void)c; Invalidate(); }
        virtual void OnMouseDown(int x, int y, int btn) { (void)x; (void)y; (void)btn; Invalidate(); }
        virtual void OnMouseMove(int x, int y) { (void)x; (void)y; }
        virtual void OnCompositorFrameDone(uint32_t frameSerial, uint32_t flags) {
            (void)frameSerial;
            (void)flags;
        }

        uint32_t GetProtocolSubmitSerial() const { return protocolSubmitSerial; }
        uint32_t GetProtocolAckSerial() const { return protocolAckSerial; }
        uint64_t GetProtocolTimeoutRecoveries() const { return protocolTimeoutRecoveries; }
        uint64_t GetProtocolThrottleSleeps() const { return protocolThrottleSleeps; }

    protected:
        bool ShouldThrottleCompositorCommit(uint64_t now_ms) const {
            if (!protocolClientMode || compositorPid == 0) return false;
            if (protocolSubmitSerial <= protocolAckSerial + 1) return false;
            if (protocolLastCommitMs == 0 || (now_ms - protocolLastCommitMs) > 1500) return false;
            return true;
        }

        void SyncCompositorRegistration(bool force) {
            if (appPid == 0) appPid = sys_get_pid();
            uint64_t now = sys_get_time_ms();

            if (protocolClientMode && protocolSubmitSerial > protocolAckSerial &&
                protocolLastCommitMs != 0 && (now - protocolLastCommitMs) > 1500) {
                protocolTimeoutRecoveries++;
                protocolAckSerial = protocolSubmitSerial;
                protocolSurfaceAnnounced = false;
            }

            uint64_t serverPid = sys_get_compositor_pid();
            if (serverPid == 0 || serverPid == appPid) {
                protocolSurfaceAnnounced = false;
                compositorPid = 0;
                protocolClientMode = false;
                protocolSubmitSerial = 0;
                protocolAckSerial = 0;
                protocolLastCommitMs = 0;
                return;
            }

            if (compositorPid != serverPid) {
                protocolSurfaceAnnounced = false;
                protocolSubmitSerial = 0;
                protocolAckSerial = 0;
                protocolLastCommitMs = 0;
            }

            compositorPid = serverPid;
            protocolClientMode = true;

            if (!force && protocolSurfaceAnnounced) return;
            if (!force && lastProtocolHelloMs != 0 && (now - lastProtocolHelloMs) < 300) return;

            MorphicCompositor::PostHello(compositorPid, (uint32_t)appPid, (uint32_t)width, (uint32_t)height);
            MorphicCompositor::PostCreateSurface(compositorPid, (uint32_t)appPid, (uint32_t)width, (uint32_t)height);
            lastProtocolHelloMs = now;
            protocolSurfaceAnnounced = true;
        }

        void NotifyCompositorCommit() {
            if (!protocolClientMode || compositorPid == 0 || appPid == 0) return;
            protocolSubmitSerial++;
            protocolLastCommitMs = sys_get_time_ms();
            MorphicCompositor::PostCommitSurfaceWithSerial(compositorPid,
                                                           (uint32_t)appPid,
                                                           protocolSubmitSerial,
                                                           (uint32_t)width,
                                                           (uint32_t)height);
        }
    };

}
