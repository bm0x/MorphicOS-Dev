#pragma once

#include <stdint.h>

/**
 * Buffer Manager - Shared buffer protocol for app windows
 * 
 * This manages buffers that apps render to, implementing a
 * producer-consumer protocol similar to Linux dma-buf:
 * 
 * 1. App creates buffer via syscall
 * 2. Buffer is mapped to app's address space
 * 3. App renders to buffer
 * 4. App signals "ready" when frame is complete
 * 5. Compositor consumes buffer (blits to display)
 * 6. Compositor signals "consumed"
 * 7. App can reuse buffer for next frame
 */

namespace BufferManager {

    /**
     * Shared buffer for app windows
     */
    struct SharedBuffer {
        uint64_t    id;             // Unique identifier
        
        uint32_t    width;          // Buffer dimensions
        uint32_t    height;
        uint32_t    pitch;          // Bytes per row
        
        void*       kernel_addr;    // Kernel virtual address
        uint64_t    phys_addr;      // Physical address
        uint64_t    user_addr;      // Userspace virtual address
        
        uint64_t    owner_pid;      // Owning process
        
        // Position on screen (set by compositor)
        int32_t     x, y;
        uint16_t    z_order;
        bool        visible;
        
        // Frame synchronization
        volatile bool ready;        // App signals frame complete
        volatile bool consumed;     // Compositor has used this frame
        
        // Dirty tracking (app can specify which parts changed)
        struct DirtyRect {
            int32_t x, y, w, h;
        };
        DirtyRect   dirty[8];
        uint32_t    dirty_count;
        
        // Timing for latency measurement
        uint64_t    submit_time;    // When app called mark_ready
        uint64_t    display_time;   // When compositor displayed it
    };
    
    //=========================================================================
    // Initialization
    //=========================================================================
    
    /**
     * Initialize buffer manager
     */
    void Init();
    
    /**
     * Shutdown buffer manager
     */
    void Shutdown();

    //=========================================================================
    // Buffer Lifecycle
    //=========================================================================
    
    /**
     * Create a shared buffer for an app window
     * @param w Width in pixels
     * @param h Height in pixels
     * @param owner_pid Process that owns this buffer
     * @return Buffer pointer, or nullptr on failure
     */
    SharedBuffer* CreateBuffer(uint32_t w, uint32_t h, uint64_t owner_pid);
    
    /**
     * Map buffer to calling process address space
     */
    void* MapToUser(SharedBuffer* buf, uint64_t pid);
    
    /**
     * Register an existing user-allocated buffer
     */
    SharedBuffer* RegisterBuffer(uint32_t w, uint32_t h, uint64_t owner_pid, uint64_t user_ptr, uint64_t phys_addr);
    
    /**
     * Destroy a buffer
     * @param buffer Buffer to destroy
     */
    void DestroyBuffer(SharedBuffer* buffer);
    
    /**
     * Get buffer by ID
     * @param id Buffer ID
     * @return Buffer pointer, or nullptr if not found
     */
    SharedBuffer* GetBuffer(uint64_t id);
    
    /**
     * Get buffer by owner PID (first match)
     * @param pid Owner PID
     * @return Buffer pointer, or nullptr if not found
     */
    SharedBuffer* GetBufferByPID(uint64_t pid);

    //=========================================================================
    // Frame Synchronization
    //=========================================================================
    
    /**
     * App signals that buffer content is ready (frame complete)
     * @param buffer Buffer to mark ready
     */
    void MarkReady(SharedBuffer* buffer);
    
    /**
     * Compositor signals that buffer was consumed
     * @param buffer Buffer that was used
     */
    void MarkConsumed(SharedBuffer* buffer);
    
    /**
     * Check if buffer is ready for composition
     * @param buffer Buffer to check
     * @return true if ready
     */
    bool IsReady(SharedBuffer* buffer);
    
    /**
     * Check if buffer was consumed (app can reuse)
     * @param buffer Buffer to check
     * @return true if consumed
     */
    bool IsConsumed(SharedBuffer* buffer);

    //=========================================================================
    // Dirty Tracking
    //=========================================================================
    
    /**
     * Mark a region of the buffer as dirty
     * @param buffer Target buffer
     * @param x Region X (relative to buffer)
     * @param y Region Y
     * @param w Region width
     * @param h Region height
     */
    void MarkDirty(SharedBuffer* buffer, int32_t x, int32_t y, int32_t w, int32_t h);
    
    /**
     * Clear dirty regions (after composition)
     * @param buffer Target buffer
     */
    void ClearDirty(SharedBuffer* buffer);

    //=========================================================================
    // Window State
    //=========================================================================
    
    /**
     * Update buffer position on screen
     * @param buffer Target buffer
     * @param x Screen X
     * @param y Screen Y
     * @param z Z-order (higher = on top)
     * @param visible Visibility flag
     */
    void SetPosition(SharedBuffer* buffer, int32_t x, int32_t y, 
                     uint16_t z, bool visible);

    //=========================================================================
    // Composition Helpers
    //=========================================================================
    
    /**
     * Get all ready buffers for composition (sorted by z-order)
     * @param out Array to fill with buffer pointers
     * @param max Maximum number of buffers to return
     * @return Number of ready buffers
     */
    uint32_t GetReadyBuffers(SharedBuffer** out, uint32_t max);
    
    /**
     * Get all visible buffers (regardless of ready state)
     * @param out Array to fill
     * @param max Maximum count
     * @return Number of visible buffers
     */
    uint32_t GetVisibleBuffers(SharedBuffer** out, uint32_t max);
}
