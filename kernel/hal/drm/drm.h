#pragma once

#include <stdint.h>

/**
 * DRM (Direct Rendering Manager) - Linux-style Display Abstraction
 * 
 * This provides a clean separation between kernel display management
 * and userspace composition, following Linux DRM/KMS architecture.
 * 
 * Key concepts:
 * - Framebuffer: A buffer that can be displayed
 * - CRTC: Display controller (manages timing, scanout)
 * - Page Flip: Atomic buffer swap synchronized with VSync
 */

namespace DRM {

    //=========================================================================
    // Core Types
    //=========================================================================
    
    /**
     * Pixel formats supported
     */
    enum PixelFormat : uint32_t {
        FORMAT_ARGB8888 = 0,    // 32-bit ARGB (alpha in high byte)
        FORMAT_XRGB8888 = 1,    // 32-bit RGB (alpha ignored)
        FORMAT_RGB888   = 2,    // 24-bit RGB (no alpha)
    };
    
    /**
     * Framebuffer - represents a displayable buffer
     * Similar to Linux drm_framebuffer
     */
    struct Framebuffer {
        uint64_t    id;             // Unique identifier
        uint32_t    width;          // Width in pixels
        uint32_t    height;         // Height in pixels  
        uint32_t    pitch;          // Bytes per row (may include padding)
        PixelFormat format;         // Pixel format
        
        void*       buffer;         // Kernel virtual address
        uint64_t    phys_addr;      // Physical address for DMA
        
        uint64_t    owner_pid;      // Owning process (0 = kernel)
        
        // Frame synchronization
        volatile bool ready;        // Buffer content is complete
        volatile bool displayed;    // Currently being scanned out
    };
    
    /**
     * CRTC - CRT Controller (display pipeline)
     * Manages a single display output
     */
    struct CRTC {
        uint64_t    id;             // Unique identifier
        
        Framebuffer* front_fb;      // Currently displayed
        Framebuffer* back_fb;       // Pending (for page flip)
        Framebuffer* render_fb;     // Being rendered to (triple buffer)
        
        // Display parameters
        uint32_t    width;
        uint32_t    height;
        uint32_t    refresh_hz;     // Refresh rate
        
        // VSync state
        bool        vsync_enabled;
        volatile uint64_t vblank_count;  // VBlank interrupt counter
        
        // Triple buffering state
        enum BufferMode {
            DOUBLE_BUFFER,          // Low latency (2 buffers)
            TRIPLE_BUFFER           // Consistent FPS (3 buffers)
        };
        BufferMode  buffer_mode;
        
        // Timing for dynamic buffer switching
        uint64_t    last_frame_time_us;
        uint64_t    avg_frame_time_us;
    };
    
    /**
     * Atomic state for page flip operations
     * Allows preparing state changes before applying
     */
    struct AtomicState {
        CRTC*       crtc;
        Framebuffer* target_fb;
        bool        pending;
        bool        async;          // Allow tearing (games)
    };

    //=========================================================================
    // Initialization
    //=========================================================================
    
    /**
     * Initialize DRM subsystem
     * Called during kernel boot after framebuffer is available
     */
    void Init(void* vram, uint32_t width, uint32_t height, uint32_t pitch);
    
    /**
     * Shutdown DRM subsystem
     */
    void Shutdown();

    //=========================================================================
    // Framebuffer Management
    //=========================================================================
    
    /**
     * Create a new framebuffer
     * @param w Width in pixels
     * @param h Height in pixels
     * @param format Pixel format
     * @param owner_pid Owning process ID (0 for kernel)
     * @return Framebuffer pointer, or nullptr on failure
     */
    Framebuffer* CreateFramebuffer(uint32_t w, uint32_t h, 
                                    PixelFormat format, uint64_t owner_pid);
    
    /**
     * Destroy a framebuffer
     * @param fb Framebuffer to destroy
     */
    void DestroyFramebuffer(Framebuffer* fb);
    
    /**
     * Get framebuffer by ID
     * @param id Framebuffer ID
     * @return Framebuffer pointer, or nullptr if not found
     */
    Framebuffer* GetFramebuffer(uint64_t id);
    
    /**
     * Mark framebuffer as ready (content complete)
     * Called by app/compositor after finishing rendering
     * @param fb Framebuffer to mark
     */
    void MarkReady(Framebuffer* fb);

    //=========================================================================
    // CRTC / Display Control
    //=========================================================================
    
    /**
     * Get the primary CRTC (main display)
     * @return Primary CRTC pointer
     */
    CRTC* GetPrimaryCRTC();
    
    /**
     * Get current display dimensions
     */
    uint32_t GetWidth();
    uint32_t GetHeight();
    uint32_t GetPitch();

    //=========================================================================
    // Page Flip / VSync
    //=========================================================================
    
    /**
     * Request a page flip (buffer swap)
     * @param crtc Display controller
     * @param fb New framebuffer to display
     * @param vsync Wait for VBlank before flip
     * @return true if flip was queued successfully
     */
    bool PageFlip(CRTC* crtc, Framebuffer* fb, bool vsync);
    
    /**
     * Wait for next VBlank
     * @param crtc Display controller
     */
    void WaitVBlank(CRTC* crtc);
    
    /**
     * Get VBlank count (for timing)
     * @param crtc Display controller
     * @return Number of VBlanks since init
     */
    uint64_t GetVBlankCount(CRTC* crtc);

    //=========================================================================
    // Composition Helpers
    //=========================================================================
    
    /**
     * Get the compositor's front buffer (shared with userspace)
     * This is the buffer that Desktop.mpk writes to
     * @return Kernel virtual address of front buffer
     */
    uint32_t* GetCompositorBuffer();
    
    /**
     * Blit a source buffer onto the compositor buffer
     * @param src Source buffer
     * @param src_w Source width
     * @param src_h Source height
     * @param dst_x Destination X
     * @param dst_y Destination Y
     * @param blend Enable alpha blending
     */
    void Blit(const uint32_t* src, uint32_t src_w, uint32_t src_h,
              int32_t dst_x, int32_t dst_y, bool blend);
    
    /**
     * Mark a region as needing update (for partial flip optimization)
     * @param x Region X
     * @param y Region Y
     * @param w Region width
     * @param h Region height
     */
    void MarkDirty(int32_t x, int32_t y, int32_t w, int32_t h);
    
    /**
     * Clear all dirty regions
     */
    void ClearDirty();
    
    /**
     * Present the compositor buffer to screen
     * Uses dirty region tracking for optimal performance
     * @param vsync Wait for VBlank
     * @return true if presented at VSync
     */
    bool Present(bool vsync);

    //=========================================================================
    // Dynamic Triple Buffering
    //=========================================================================
    
    /**
     * Record frame render time for dynamic buffer switching
     * @param render_time_us Time taken to render frame in microseconds
     */
    void RecordFrameTime(uint64_t render_time_us);
    
    /**
     * Get current buffer mode
     * @return Current buffering mode
     */
    CRTC::BufferMode GetBufferMode();
    
    /**
     * Force a specific buffer mode (for testing)
     * @param mode Buffer mode to use
     */
    void SetBufferMode(CRTC::BufferMode mode);
}
