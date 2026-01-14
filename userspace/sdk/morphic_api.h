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
        
        // Basic Font 8x16 (embedded in apps usually, or use system font if available)
        // For now, apps need their own font renderer or use a simple shared one.
        // We will assume a simple DrawChar is implemented by the app or added later.
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

        virtual bool Init() {
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
            
            // Use this buffer directly (Kernel handles double buffering)
            backbuffer = (uint32_t*)addr;
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

                // Flip backbuffer to screen
                sys_video_flip(backbuffer);
            }
        }
        
        virtual void OnKeyDown(char c) {}
        virtual void OnMouseDown(int x, int y, int btn) {}
        virtual void OnMouseMove(int x, int y) {}
    };

}
