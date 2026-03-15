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
        // Basic memory copy/set since we might lack stdlib
        // Optimized memset - uses 64-bit words when possible
        static void* memset(void* dest, int c, size_t n) {
            uint8_t* p = (uint8_t*)dest;
            // Fill byte pattern
            uint8_t byte = (uint8_t)c;
            // Fast path for aligned large fills
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
            while(n--) *p++ = byte;
            return dest;
        }
        
        // Optimized 32-bit fill for framebuffers
        static void memset32(uint32_t* dest, uint32_t value, size_t count) {
            // Unrolled loop for speed
            size_t i = 0;
            for (; i + 4 <= count; i += 4) {
                dest[i] = value;
                dest[i+1] = value;
                dest[i+2] = value;
                dest[i+3] = value;
            }
            for (; i < count; i++) dest[i] = value;
        }
        
        // Optimized memcpy - uses 64-bit words when possible
        static void* memcpy(void* dest, const void* src, size_t n) {
            // Fast path for aligned copies
            if (n >= 8 && ((uintptr_t)dest & 7) == 0 && ((uintptr_t)src & 7) == 0) {
                uint64_t* d64 = (uint64_t*)dest;
                const uint64_t* s64 = (const uint64_t*)src;
                size_t count64 = n / 8;
                for (size_t i = 0; i < count64; i++) d64[i] = s64[i];
                
                uint8_t* d = (uint8_t*)dest + count64 * 8;
                const uint8_t* s = (const uint8_t*)src + count64 * 8;
                n -= count64 * 8;
                while(n--) *d++ = *s++;
            } else {
                uint8_t* d = (uint8_t*)dest;
                const uint8_t* s = (const uint8_t*)src;
                while(n--) *d++ = *s++;
            }
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
            Helpers::memset32(fb, color, width * height);
        }

        void PutPixel(int x, int y, uint32_t color) {
            if (x < 0 || y < 0 || (uint64_t)x >= width || (uint64_t)y >= height) return;
            fb[y * pitch + x] = color;
        }

        void FillRect(int x, int y, int w, int h, uint32_t color) {
            // Clipping
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > (int)width) w = (int)width - x;
            if (y + h > (int)height) h = (int)height - y;
            if (w <= 0 || h <= 0) return;
            
            // Row-based fill (much faster than pixel-by-pixel)
            for (int j = 0; j < h; j++) {
                Helpers::memset32(fb + (y + j) * pitch + x, color, w);
            }
        }
        
        // VGA 8x16 Font Rendering - Optimized
        void DrawChar(int x, int y, char c, uint32_t color, int scale = 1) {
            unsigned char uc = (unsigned char)c;
            if (uc > 0x7F) uc = '?'; 
            
            const unsigned char* glyph = &font8x16[uc * 16];
            
            if (scale == 1) {
                // Bounds check
                if (x < 0 || y < 0 || (uint64_t)(x + 8) > width || (uint64_t)(y + 16) > height) {
                     for (int r = 0; r < 16; r++) {
                        if (y+r < 0 || (uint64_t)(y+r) >= height) continue;
                         unsigned char row_bits = glyph[r];
                         for (int col = 0; col < 8; col++) {
                             if (row_bits & (0x80 >> col)) PutPixel(x + col, y + r, color);
                         }
                     }
                     return;
                }
                // Fast path for bulk text
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
                    for (int col = 0; col < 8; col++) {
                        if (row_bits & (0x80 >> col)) {
                            FillRect(x + col * scale, y + r * scale, scale, scale, color);
                        }
                    }
                }
            }
        }
        
        void DrawText(int x, int y, const char* text, uint32_t color, int scale = 1) {
            int cx = x;
            while (*text) {
                DrawChar(cx, y, *text, color, scale);
                cx += 9 * scale; // 8px char + 1px spacing
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
        uint64_t appPid;
        uint64_t compositorPid;
        uint64_t lastProtocolHelloMs;
        bool protocolClientMode;
        bool protocolSurfaceAnnounced;

    public:
        Window(uint32_t w = 0, uint32_t h = 0) 
            : backbuffer(nullptr), width(0), height(0), 
                            requestW(w), requestH(h), running(false),
                            appPid(0), compositorPid(0), lastProtocolHelloMs(0),
                              protocolClientMode(false), protocolSurfaceAnnounced(false) {}
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
            appPid = sys_get_pid();
            SyncCompositorRegistration(true);
            return true;
        }

        virtual void OnUpdate() = 0;
        virtual void OnRender(Graphics& g) = 0;
        
        // Flag to trigger redraw
        bool needsRedraw;

        void Invalidate() {
            needsRedraw = true;
        }

        void Run() {
            if (!Init()) {
                 sys_debug_print("Failed to init window\n");
                 return;
            }

            Graphics g(backbuffer, width, height, width); // Pitch = width (linear)
            
            // Initial draw
            needsRedraw = true;
            
            // Progressive idle sleep - start at 16ms, increase to 100ms when truly idle
            uint32_t idleCounter = 0;
            const uint32_t MAX_IDLE_SLEEP = 100;  // 100ms = 10 FPS when completely idle
            const uint32_t MIN_IDLE_SLEEP = 16;   // 16ms = ~60 FPS when active

            while (running) {
                SyncCompositorRegistration(false);

                // Process all pending events first (responsive input)
                OSEvent ev;
                bool hadEvent = false;
                
                // Burst process events
                int maxEvents = 20; 
                while (sys_get_event(&ev) && maxEvents--) {
                    hadEvent = true;
                    // Trigger redraw on any input
                    needsRedraw = true;
                    idleCounter = 0;  // Reset idle counter on any event
                    
                    // Handle window close request from Desktop
                    if (ev.type == OSEvent::WINDOW_DESTROYED) {
                        running = false;
                        break;
                    }
                    
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
                        OnMouseMove(ev.dx, ev.dy);
                    }
                }

                OnUpdate(); // Animation logic might set needsRedraw
                
                if (needsRedraw) {
                    OnRender(g);

                    // Commit Frame: Copy scratch to kernel buffer
                    if (backbuffer != kernelBuffer) {
                        // Copy in 64-bit chunks for speed
                        uint64_t* src = (uint64_t*)backbuffer;
                        uint64_t* dst = (uint64_t*)kernelBuffer;
                        size_t count = (width * height * 4) / 8;
                        for (size_t i = 0; i < count; i++) dst[i] = src[i];
                    }

                    // Flip/Notify Kernel
                    sys_video_flip(kernelBuffer);
                    NotifyCompositorCommit();
                    needsRedraw = false;
                    idleCounter = 0;  // Reset idle counter on redraw
                    
                    // NOTE: Removed sys_yield() - timer preemption is sufficient
                    // and explicit yields cause too many context switches
                } else {
                    // No redraw needed - progressive sleep
                    idleCounter++;
                    
                    // Calculate sleep time: start at 16ms, increase to 100ms over ~60 frames
                    uint32_t sleepTime = MIN_IDLE_SLEEP + (idleCounter / 4);
                    if (sleepTime > MAX_IDLE_SLEEP) sleepTime = MAX_IDLE_SLEEP;
                    
                    sys_sleep(sleepTime);
                }
            }  // while (running)
        }  // Run()
        
        virtual void OnKeyDown(char c) { Invalidate(); }
        virtual void OnMouseDown(int x, int y, int btn) { Invalidate(); }
        virtual void OnMouseMove(int x, int y) {
            // DO NOT invalidate on every mouse move - causes excessive redraws
            // Apps that need hover effects should override and call Invalidate() themselves
        }

    protected:
        void SyncCompositorRegistration(bool force) {
            if (appPid == 0) {
                appPid = sys_get_pid();
            }

            uint64_t serverPid = sys_get_compositor_pid();
            if (serverPid == 0 || serverPid == appPid) {
                protocolSurfaceAnnounced = false;
                compositorPid = 0;
                protocolClientMode = false;
                return;
            }

            if (compositorPid != serverPid) {
                protocolSurfaceAnnounced = false;
            }

            compositorPid = serverPid;
            protocolClientMode = true;

            uint64_t now = sys_get_time_ms();
            if (!force && protocolSurfaceAnnounced) {
                return;
            }

            MorphicCompositor::PostHello(compositorPid, (uint32_t)appPid, (uint32_t)width, (uint32_t)height);
            MorphicCompositor::PostCreateSurface(compositorPid, (uint32_t)appPid, (uint32_t)width, (uint32_t)height);
            lastProtocolHelloMs = now;
            protocolSurfaceAnnounced = true;
        }

        void NotifyCompositorCommit() {
            if (!protocolClientMode || compositorPid == 0 || appPid == 0) {
                return;
            }
            MorphicCompositor::PostCommitSurface(compositorPid,
                                                 (uint32_t)appPid,
                                                 (uint32_t)width,
                                                 (uint32_t)height);
        }
    };

}
