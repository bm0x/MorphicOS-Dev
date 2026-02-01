#include "drm.h"
#include "buffer_manager.h"
#include "../video/graphics.h"
#include "../serial/uart.h"
#include "../../mm/heap.h"
#include "../../mm/pmm.h"
#include "../platform.h"
#include "../../drivers/gpu/bga.h"

namespace DRM {

    //=========================================================================
    // Internal State
    //=========================================================================
    
    // Maximum framebuffers and CRTCs
    static constexpr uint32_t MAX_FRAMEBUFFERS = 64;
    static constexpr uint32_t MAX_CRTCS = 4;
    
    // Framebuffer storage
    static Framebuffer framebuffers[MAX_FRAMEBUFFERS];
    static uint32_t framebuffer_count = 0;
    static uint64_t next_fb_id = 1;
    
    // CRTC storage (we have one primary display)
    static CRTC crtcs[MAX_CRTCS];
    static uint32_t crtc_count = 0;
    static CRTC* primary_crtc = nullptr;
    
    // Compositor buffer (shared with userspace Desktop.mpk)
    static uint32_t* compositor_buffer = nullptr;
    static uint32_t screen_width = 0;
    static uint32_t screen_height = 0;
    static uint32_t screen_pitch = 0;
    
    // VRAM direct access
    static uint32_t* vram_buffer = nullptr;
    
    // Dirty region tracking
    static constexpr uint32_t MAX_DIRTY_RECTS = 16;
    struct DirtyRect {
        int32_t x, y, w, h;
        bool active;
    };
    static DirtyRect dirty_rects[MAX_DIRTY_RECTS];
    static uint32_t dirty_count = 0;
    static bool full_dirty = true;
    
    // Lock for thread-safe operations
    static volatile uint32_t drm_lock = 0;
    
    // GPU driver (for VSync)
    static BGADriver bga_driver;
    static bool gpu_enabled = false;
    
    // Dynamic triple buffering thresholds
    static constexpr uint64_t FRAME_TIME_60HZ_US = 16666;  // ~16.67ms
    static constexpr uint64_t TRIPLE_BUFFER_THRESHOLD = FRAME_TIME_60HZ_US * 70 / 100;  // 70%
    static constexpr uint64_t DOUBLE_BUFFER_THRESHOLD = FRAME_TIME_60HZ_US * 40 / 100;  // 40%
    
    //=========================================================================
    // Internal Helpers
    //=========================================================================
    
    static void AcquireLock() {
        while (__sync_lock_test_and_set(&drm_lock, 1)) {
            asm volatile("pause");
        }
    }
    
    static void ReleaseLock() {
        __sync_lock_release(&drm_lock);
    }
    
    // SIMD-optimized memory copy (from blit_fast.S)
    extern "C" void blit_fast_32(void* dest, void* src, size_t count);
    
    //=========================================================================
    // Initialization
    //=========================================================================
    
    void Init(void* vram, uint32_t width, uint32_t height, uint32_t pitch) {
        UART::Write("[DRM] Initializing...\n");
        BufferManager::Init(); // Initialize shared buffer subsystem
        
        // Store display parameters
        vram_buffer = (uint32_t*)vram;
        screen_width = width;
        screen_height = height;
        screen_pitch = pitch;
        
        // Allocate compositor buffer (userspace renders here)
        uint64_t buffer_size = (uint64_t)width * height * 4;
        compositor_buffer = (uint32_t*)KHeap::Allocate(buffer_size);
        
        if (!compositor_buffer) {
            UART::Write("[DRM] ERROR: Failed to allocate compositor buffer!\n");
            return;
        }
        
        // Clear compositor buffer
        for (uint32_t i = 0; i < width * height; i++) {
            compositor_buffer[i] = 0xFF181818;  // Dark gray
        }
        
        // Initialize primary CRTC
        primary_crtc = &crtcs[0];
        primary_crtc->id = 1;
        primary_crtc->width = width;
        primary_crtc->height = height;
        primary_crtc->refresh_hz = 60;
        primary_crtc->vsync_enabled = true;
        primary_crtc->vblank_count = 0;
        primary_crtc->buffer_mode = CRTC::DOUBLE_BUFFER;
        primary_crtc->last_frame_time_us = 0;
        primary_crtc->avg_frame_time_us = 8000;  // Assume 8ms initially
        
        // Create front framebuffer (points to compositor buffer)
        primary_crtc->front_fb = CreateFramebuffer(width, height, FORMAT_ARGB8888, 0);
        if (primary_crtc->front_fb) {
            // Override buffer to point to our compositor buffer
            primary_crtc->front_fb->buffer = compositor_buffer;
        }
        
        primary_crtc->back_fb = nullptr;
        primary_crtc->render_fb = nullptr;
        crtc_count = 1;
        
        // Try to initialize GPU for VSync
        // Note: In simple mode, we use VGA ports for VSync detection
        gpu_enabled = false;  // Will be set by BGA init if available
        
        // Clear dirty tracking
        full_dirty = true;
        dirty_count = 0;
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirty_rects[i].active = false;
        }
        
        UART::Write("[DRM] Init complete: ");
        UART::WriteDec(width);
        UART::Write("x");
        UART::WriteDec(height);
        UART::Write(" @ ");
        UART::WriteDec(primary_crtc->refresh_hz);
        UART::Write("Hz\n");
    }
    
    void Shutdown() {
        if (compositor_buffer) {
            KHeap::Free(compositor_buffer);
            compositor_buffer = nullptr;
        }
        framebuffer_count = 0;
        crtc_count = 0;
        primary_crtc = nullptr;
    }

    //=========================================================================
    // Framebuffer Management
    //=========================================================================
    
    Framebuffer* CreateFramebuffer(uint32_t w, uint32_t h, 
                                    PixelFormat format, uint64_t owner_pid) {
        AcquireLock();
        
        if (framebuffer_count >= MAX_FRAMEBUFFERS) {
            ReleaseLock();
            return nullptr;
        }
        
        // Find free slot
        Framebuffer* fb = nullptr;
        for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; i++) {
            if (framebuffers[i].id == 0) {
                fb = &framebuffers[i];
                break;
            }
        }
        
        if (!fb) {
            ReleaseLock();
            return nullptr;
        }
        
        // Calculate buffer size
        uint32_t bpp = (format == FORMAT_RGB888) ? 3 : 4;
        uint32_t pitch = w * bpp;
        uint64_t size = (uint64_t)pitch * h;
        
        // Allocate buffer
        void* buffer = KHeap::Allocate(size);
        if (!buffer) {
            ReleaseLock();
            return nullptr;
        }
        
        // Initialize framebuffer
        fb->id = next_fb_id++;
        fb->width = w;
        fb->height = h;
        fb->pitch = pitch;
        fb->format = format;
        fb->buffer = buffer;
        fb->phys_addr = 0;  // Will be set if needed for DMA
        fb->owner_pid = owner_pid;
        fb->ready = false;
        fb->displayed = false;
        
        framebuffer_count++;
        
        ReleaseLock();
        return fb;
    }
    
    void DestroyFramebuffer(Framebuffer* fb) {
        if (!fb || fb->id == 0) return;
        
        AcquireLock();
        
        // Don't free compositor buffer (it's special)
        if (fb->buffer && fb->buffer != compositor_buffer) {
            KHeap::Free(fb->buffer);
        }
        
        fb->id = 0;
        fb->buffer = nullptr;
        framebuffer_count--;
        
        ReleaseLock();
    }
    
    Framebuffer* GetFramebuffer(uint64_t id) {
        for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; i++) {
            if (framebuffers[i].id == id) {
                return &framebuffers[i];
            }
        }
        return nullptr;
    }
    
    void MarkReady(Framebuffer* fb) {
        if (fb) {
            fb->ready = true;
        }
    }

    //=========================================================================
    // CRTC / Display Control
    //=========================================================================
    
    CRTC* GetPrimaryCRTC() {
        return primary_crtc;
    }
    
    uint32_t GetWidth() {
        return screen_width;
    }
    
    uint32_t GetHeight() {
        return screen_height;
    }
    
    uint32_t GetPitch() {
        return screen_pitch;
    }

    //=========================================================================
    // Page Flip / VSync
    //=========================================================================
    
    // VGA VSync detection (fallback when no GPU driver)
    static inline uint8_t inb(uint16_t port) {
        uint8_t result;
        asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
        return result;
    }
    
    static void WaitVBlankVGA() {
        // Wait for end of current retrace (if in retrace)
        while (inb(0x3DA) & 0x08) {
            asm volatile("pause");
        }
        // Wait for start of retrace
        while (!(inb(0x3DA) & 0x08)) {
            asm volatile("pause");
        }
    }
    
    bool PageFlip(CRTC* crtc, Framebuffer* fb, bool vsync) {
        if (!crtc || !fb) return false;
        
        // Wait for VBlank if requested
        if (vsync) {
            WaitVBlank(crtc);
        }
        
        // Swap buffers
        crtc->front_fb = fb;
        crtc->vblank_count++;
        
        return true;
    }
    
    void WaitVBlank(CRTC* crtc) {
        (void)crtc;
        
        // Use GPU driver if available, otherwise VGA ports
        if (gpu_enabled) {
            bga_driver.WaitVSync();
        } else {
            WaitVBlankVGA();
        }
    }
    
    uint64_t GetVBlankCount(CRTC* crtc) {
        return crtc ? crtc->vblank_count : 0;
    }

    //=========================================================================
    // Composition Helpers
    //=========================================================================
    
    uint32_t* GetCompositorBuffer() {
        return compositor_buffer;
    }
    
    void Blit(const uint32_t* src, uint32_t src_w, uint32_t src_h,
              int32_t dst_x, int32_t dst_y, bool blend) {
        
        if (!src || !compositor_buffer) return;
        
        // Clip to screen bounds
        int32_t start_x = (dst_x < 0) ? -dst_x : 0;
        int32_t start_y = (dst_y < 0) ? -dst_y : 0;
        int32_t end_x = (dst_x + (int32_t)src_w > (int32_t)screen_width) 
                        ? (int32_t)screen_width - dst_x : (int32_t)src_w;
        int32_t end_y = (dst_y + (int32_t)src_h > (int32_t)screen_height)
                        ? (int32_t)screen_height - dst_y : (int32_t)src_h;
        
        if (end_x <= start_x || end_y <= start_y) return;
        
        if (blend) {
            // Alpha blending (slow path)
            for (int32_t y = start_y; y < end_y; y++) {
                for (int32_t x = start_x; x < end_x; x++) {
                    uint32_t src_pixel = src[y * src_w + x];
                    uint32_t alpha = (src_pixel >> 24) & 0xFF;
                    
                    if (alpha == 255) {
                        compositor_buffer[(dst_y + y) * screen_width + (dst_x + x)] = src_pixel;
                    } else if (alpha > 0) {
                        uint32_t dst_pixel = compositor_buffer[(dst_y + y) * screen_width + (dst_x + x)];
                        
                        uint32_t inv_alpha = 255 - alpha;
                        uint32_t r = ((((src_pixel >> 16) & 0xFF) * alpha) + 
                                      (((dst_pixel >> 16) & 0xFF) * inv_alpha)) >> 8;
                        uint32_t g = ((((src_pixel >> 8) & 0xFF) * alpha) + 
                                      (((dst_pixel >> 8) & 0xFF) * inv_alpha)) >> 8;
                        uint32_t b = (((src_pixel & 0xFF) * alpha) + 
                                      ((dst_pixel & 0xFF) * inv_alpha)) >> 8;
                        
                        compositor_buffer[(dst_y + y) * screen_width + (dst_x + x)] = 
                            0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        } else {
            // Opaque blit (fast path)
            for (int32_t y = start_y; y < end_y; y++) {
                uint32_t* dst_row = &compositor_buffer[(dst_y + y) * screen_width + dst_x + start_x];
                const uint32_t* src_row = &src[y * src_w + start_x];
                uint32_t copy_width = end_x - start_x;
                
                // Use SIMD copy if available
                blit_fast_32(dst_row, (void*)src_row, copy_width);
            }
        }
    }
    
    void MarkDirty(int32_t x, int32_t y, int32_t w, int32_t h) {
        // Clamp to screen
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int32_t)screen_width) w = (int32_t)screen_width - x;
        if (y + h > (int32_t)screen_height) h = (int32_t)screen_height - y;
        if (w <= 0 || h <= 0) return;
        
        AcquireLock();
        
        // Find free slot
        if (dirty_count < MAX_DIRTY_RECTS) {
            for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
                if (!dirty_rects[i].active) {
                    dirty_rects[i].x = x;
                    dirty_rects[i].y = y;
                    dirty_rects[i].w = w;
                    dirty_rects[i].h = h;
                    dirty_rects[i].active = true;
                    dirty_count++;
                    break;
                }
            }
        } else {
            // Too many rects, mark full screen dirty
            full_dirty = true;
        }
        
        ReleaseLock();
    }
    
    void ClearDirty() {
        AcquireLock();
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirty_rects[i].active = false;
        }
        dirty_count = 0;
        full_dirty = false;
        ReleaseLock();
    }
    
    bool Present(bool vsync) {
        if (!compositor_buffer || !vram_buffer) return false;
        
        // Wait for VBlank if requested
        if (vsync) {
            WaitVBlank(primary_crtc);
        }
        
        AcquireLock();
        
        if (full_dirty) {
            // Full screen copy
            blit_fast_32(vram_buffer, compositor_buffer, screen_width * screen_height);
        } else if (dirty_count > 0) {
            // Copy only dirty regions
            for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
                if (!dirty_rects[i].active) continue;
                
                DirtyRect& r = dirty_rects[i];
                for (int32_t y = 0; y < r.h; y++) {
                    uint32_t offset = (r.y + y) * screen_width + r.x;
                    blit_fast_32(&vram_buffer[offset], &compositor_buffer[offset], r.w);
                }
            }
        }
        
        // Clear dirty state
        for (uint32_t i = 0; i < MAX_DIRTY_RECTS; i++) {
            dirty_rects[i].active = false;
        }
        dirty_count = 0;
        full_dirty = false;
        
        if (primary_crtc) {
            primary_crtc->vblank_count++;
        }
        
        ReleaseLock();
        
        return vsync;  // Return true if we waited for vsync
    }

    //=========================================================================
    // Dynamic Triple Buffering
    //=========================================================================
    
    void RecordFrameTime(uint64_t render_time_us) {
        if (!primary_crtc) return;
        
        primary_crtc->last_frame_time_us = render_time_us;
        
        // Exponential moving average
        primary_crtc->avg_frame_time_us = 
            (primary_crtc->avg_frame_time_us * 7 + render_time_us) / 8;
        
        // Dynamic buffer mode switching
        if (primary_crtc->avg_frame_time_us > TRIPLE_BUFFER_THRESHOLD) {
            // Heavy load - switch to triple buffering
            if (primary_crtc->buffer_mode == CRTC::DOUBLE_BUFFER) {
                primary_crtc->buffer_mode = CRTC::TRIPLE_BUFFER;
                UART::Write("[DRM] Switched to triple buffering\n");
            }
        } else if (primary_crtc->avg_frame_time_us < DOUBLE_BUFFER_THRESHOLD) {
            // Light load - switch back to double buffering
            if (primary_crtc->buffer_mode == CRTC::TRIPLE_BUFFER) {
                primary_crtc->buffer_mode = CRTC::DOUBLE_BUFFER;
                UART::Write("[DRM] Switched to double buffering\n");
            }
        }
    }
    
    CRTC::BufferMode GetBufferMode() {
        return primary_crtc ? primary_crtc->buffer_mode : CRTC::DOUBLE_BUFFER;
    }
    
    void SetBufferMode(CRTC::BufferMode mode) {
        if (primary_crtc) {
            primary_crtc->buffer_mode = mode;
        }
    }
}
