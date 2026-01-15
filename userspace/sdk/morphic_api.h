#pragma once
#include "morphic_syscalls.h"
#include "os_event.h"
#include <stdint.h>
#include <stddef.h>

// Standard colors
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_BLACK 0xFF000000
#define COLOR_RED   0xFFFF0000
#define COLOR_GREEN 0xFF00FF00
#define COLOR_BLUE  0xFF0000FF

namespace MorphicAPI {

    class Helpers {
    public:
        // Basic memory copy/set since we might lack stdlib
        static void* memset(void* dest, int c, size_t n) {
            uint8_t* p = (uint8_t*)dest;
            while(n--) *p++ = (uint8_t)c;
            return dest;
        }
        
        static void* memcpy(void* dest, const void* src, size_t n) {
            uint8_t* d = (uint8_t*)dest;
            const uint8_t* s = (const uint8_t*)src;
            while(n--) *d++ = *s++;
            return dest;
        }
        
        static size_t strlen(const char* s) {
            size_t len = 0;
            while(s[len]) len++;
            return len;
        }
    };

    class Graphics {
    protected:
        uint32_t* fb;
        uint64_t width, height, pitch;

    public:
        Graphics(uint32_t* framebuffer, uint64_t w, uint64_t h, uint64_t p) 
            : fb(framebuffer), width(w), height(h), pitch(p) {}

        void Clear(uint32_t color) {
            for (uint64_t i = 0; i < width * height; i++) {
                fb[i] = color;
            }
        }

        void PutPixel(int x, int y, uint32_t color) {
            if (x < 0 || y < 0 || (uint64_t)x >= width || (uint64_t)y >= height) return;
            fb[y * pitch + x] = color;
        }

        void FillRect(int x, int y, int w, int h, uint32_t color) {
            for (int j = 0; j < h; j++) {
                for (int i = 0; i < w; i++) {
                    PutPixel(x + i, y + j, color);
                }
            }
        }
        
        // Embedded 5x7 Font Data
        static uint8_t GetFontRow(char c, int row) {
            if (row < 0 || row >= 7) return 0;
            // ToUpper
             if (c >= 'a' && c <= 'z') c -= 32;
             
             // Minimal Set
             if (c >= '0' && c <= '9') {
                 static const uint8_t d[10][7] = {
                    {0x1E,0x11,0x13,0x15,0x19,0x11,0x1E}, // 0
                    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
                    {0x1E,0x01,0x01,0x1E,0x10,0x10,0x1F}, // 2
                    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, // 3
                    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
                    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, // 5
                    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x1E}, // 6
                    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
                    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 8
                    {0x1E,0x11,0x11,0x1F,0x01,0x01,0x0E}  // 9
                 };
                 return d[c - '0'][row];
             }
             // Ops
             if (c == '+') { static const uint8_t r[7]={0,4,4,31,4,4,0}; return r[row]; }
             if (c == '-') { static const uint8_t r[7]={0,0,0,31,0,0,0}; return r[row]; }
             if (c == '*') { static const uint8_t r[7]={0,21,14,4,14,21,0}; return r[row]; }
             if (c == '/') { static const uint8_t r[7]={1,2,4,8,16,0,0}; return r[row]; }
             if (c == '=') { static const uint8_t r[7]={0,0,31,0,31,0,0}; return r[row]; }
             if (c == 'C') { static const uint8_t r[7]={14,17,16,16,16,17,14}; return r[row]; } // Clear
             
             // Letters (A-Z fallback partial) - only needed for "F" maybe? Or labels.
             // Very mini implementation for calculator
             if (c == 'A') { static const uint8_t r[7]={14,17,17,31,17,17,17}; return r[row]; }
             if (c == 'B') { static const uint8_t r[7]={30,17,17,30,17,17,30}; return r[row]; }
             if (c == 'E') { static const uint8_t r[7]={31,16,16,30,16,16,31}; return r[row]; }
             if (c == 'R') { static const uint8_t r[7]={30,17,17,30,20,18,17}; return r[row]; }
             if (c == 'O') { static const uint8_t r[7]={14,17,17,17,17,17,14}; return r[row]; }
             if (c == 'R') { static const uint8_t r[7]={30,17,17,30,20,18,17}; return r[row]; }

             // Default block
             return 0x1F;
        }

        void DrawChar(int x, int y, char c, uint32_t color, int scale = 1) {
            for (int r = 0; r < 7; r++) {
                uint8_t bits = GetFontRow(c, r);
                for (int col = 0; col < 5; col++) {
                    if (bits & (1 << (4 - col))) {
                        FillRect(x + col * scale, y + r * scale, scale, scale, color);
                    }
                }
            }
        }
        
        void DrawText(int x, int y, const char* text, uint32_t color, int scale = 1) {
            int cx = x;
            while (*text) {
                DrawChar(cx, y, *text, color, scale);
                cx += 6 * scale; // 5 + 1 spacing
                text++;
            }
        }
    };

    class Window {
    protected:
        uint32_t* backbuffer;
        uint64_t width, height;
        uint64_t sys_width, sys_height;
        uint32_t requestW, requestH;
        bool running;

    public:
        Window(uint32_t w = 0, uint32_t h = 0) 
            : backbuffer(nullptr), width(0), height(0), 
              requestW(w), requestH(h), running(false) {}
        virtual ~Window() {}

        // Static Scratch Buffer for Double Buffering (Per-Process)
        // Solves flickering by drawing here first, then copying to kernel buffer.
        // static uint32_t s_scratchBuffer[1024 * 768]; // Moved to Init() static local

        uint32_t* kernelBuffer; // The shared buffer mapped to kernel layer

        virtual bool Init() {
             // Increase buffer to 4K (3840x2160) to support large windows/maximized apps
             const uint64_t MAX_PIXELS = 3840 * 2160;
             const uint64_t BUF_SIZE = MAX_PIXELS * 4;

             static uint32_t* s_scratchBuffer = nullptr;
             if (!s_scratchBuffer) {
                 uint64_t addr = sys_alloc_backbuffer(BUF_SIZE);
                 s_scratchBuffer = (uint32_t*)addr;
             }

            // Get Screen Dimensions
            uint64_t info = sys_get_screen_info();
            sys_width = (info >> 32) & 0xFFFFFFFF;
            sys_height = info & 0xFFFFFFFF;
            if (sys_width == 0) sys_width = 1280;
            if (sys_height == 0) sys_height = 800;
            
            void* addr = nullptr;
            
            if (requestW > 0 && requestH > 0) {
                // Request specific window size
                addr = sys_create_window(requestW, requestH, 0);
                width = requestW;
                height = requestH;
            } else {
                // Request fullscreen window (Legacy)
                addr = sys_video_map();
                width = sys_width;
                height = sys_height;
            }

            if (!addr) return false;
            
            // Double Buffering Setup
            kernelBuffer = (uint32_t*)addr;
            
            // If we have scratch buffer and it fits
            if (s_scratchBuffer && (width * height <= MAX_PIXELS)) {
                backbuffer = s_scratchBuffer;
            } else {
                // Fallback to single buffering
                backbuffer = kernelBuffer;
            }
            
            running = true;
            return true;
        }

        virtual void OnUpdate() = 0;
        virtual void OnRender(Graphics& g) = 0;
        
        void Run() {
            if (!Init()) {
                 sys_debug_print("Failed to init window\n");
                 return;
            }

            Graphics g(backbuffer, width, height, width); // Pitch = width (linear)

            while (running) {
                OSEvent ev;
                while (sys_get_event(&ev)) {
                    if (ev.type == OSEvent::KEY_PRESS) {
                        if (ev.ascii == 27) { // ESC
                             running = false; 
                        }
                        OnKeyDown(ev.ascii);
                    }
                    if (ev.type == OSEvent::MOUSE_CLICK) {
                        OnMouseDown(ev.dx, ev.dy, ev.buttons);
                    }
                    if (ev.type == OSEvent::MOUSE_MOVE) {
                        OnMouseMove(ev.dx, ev.dy); // Note: kernel sends absolute pos usually or delta
                    }
                }

                OnUpdate();
                OnRender(g);

                // Commit Frame: Copy scratch to kernel buffer
                if (backbuffer != kernelBuffer) {
                    Helpers::memcpy(kernelBuffer, backbuffer, width * height * 4);
                }

                // Flip/Notify Kernel
                sys_video_flip(kernelBuffer);
            }
        }
        
        virtual void OnKeyDown(char c) {}
        virtual void OnMouseDown(int x, int y, int btn) {}
        virtual void OnMouseMove(int x, int y) {}
    };

}
