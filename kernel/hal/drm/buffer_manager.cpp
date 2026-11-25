#include "buffer_manager.h"
#include "../../mm/pmm.h"
#include "../serial/uart.h"
#include "../../utils/std.h"
#include "../../arch/common/mmu.h"

namespace BufferManager {

    //=========================================================================
    // Internal State
    //=========================================================================
    
    // Limits
    static constexpr uint32_t MAX_BUFFERS = 64;
    
    // Buffer storage
    static SharedBuffer buffers[MAX_BUFFERS];
    static uint32_t active_buffer_count = 0;
    static uint64_t next_buffer_id = 1;
    
    // Thread safety
    static volatile uint32_t bm_lock = 0;
    
    static void AcquireLock() {
        while (__sync_lock_test_and_set(&bm_lock, 1)) {
            asm volatile("pause");
        }
    }
    
    static void ReleaseLock() {
        __sync_lock_release(&bm_lock);
    }
    
    //=========================================================================
    // Initialization
    //=========================================================================
    
    void Init() {
        UART::Write("[BufferManager] Initializing...\n");
        
        // Zero out buffer storage
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            buffers[i].id = 0;
            buffers[i].kernel_addr = nullptr;
        }
        
        active_buffer_count = 0;
        next_buffer_id = 1;
        bm_lock = 0;
    }
    
    void Shutdown() {
        UART::Write("[BufferManager] Shutting down...\n");
        
        // Free all buffers (allocated via PMM, never kfree)
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id != 0 && buffers[i].phys_addr != 0) {
                uint64_t pages = ((uint64_t)buffers[i].pitch * buffers[i].height + 4095) / 4096;
                PMM::FreePages((void*)buffers[i].phys_addr, pages);
                buffers[i].kernel_addr = nullptr;
                buffers[i].phys_addr = 0;
                buffers[i].user_addr = 0;
                buffers[i].id = 0;
            }
        }
        active_buffer_count = 0;
    }

    //=========================================================================
    // Buffer Lifecycle
    //=========================================================================
    
    SharedBuffer* CreateBuffer(uint32_t w, uint32_t h, uint64_t owner_pid) {
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return nullptr;
        
        AcquireLock();
        
        if (active_buffer_count >= MAX_BUFFERS) {
            ReleaseLock();
            return nullptr;
        }
        
        // Find free slot
        SharedBuffer* buf = nullptr;
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id == 0) {
                buf = &buffers[i];
                break;
            }
        }
        
        if (!buf) {
            ReleaseLock();
            return nullptr;
        }
        
        // Allocate page-aligned physical pages from PMM (zero KHeap exposure to Ring 3)
        uint32_t pitch = w * 4; // ARGB32
        uint64_t size = (uint64_t)pitch * h;
        uint64_t pages = (size + 4095) / 4096;
        
        void* mem = PMM::AllocContiguous(pages);
        if (!mem) {
            UART::Write("[BufferManager] PMM::AllocContiguous failed\n");
            ReleaseLock();
            return nullptr;
        }
        
        // Initialize struct
        buf->id = next_buffer_id++;
        buf->width = w;
        buf->height = h;
        buf->pitch = pitch;
        buf->kernel_addr = mem; // Identity-mapped physical address for Ring 0
        buf->phys_addr = (uint64_t)mem;
        buf->user_addr = 0; // Set by MapToUser
        buf->owner_pid = owner_pid;
        
        // Default position
        buf->x = 0;
        buf->y = 0;
        buf->z_order = 0;
        buf->visible = false;
        
        // Sync state
        buf->ready = false;
        buf->consumed = true; // Born consumed so app can draw immediately
        buf->submit_time = 0;
        buf->display_time = 0;
        
        // Dirty state
        buf->dirty_count = 0;
        
        active_buffer_count++;
        
        // Optimization: Pre-clear buffer to black effectively
        // (Assuming Heap::Alloc doesn't zero)
        uint32_t* p = (uint32_t*)mem;
        for(uint64_t k=0; k < (size/4); k++) p[k] = 0xFF000000;
        
        UART::Write("[BufferManager] Created buffer ID: ");
        UART::WriteDec(buf->id);
        UART::Write(" for PID: ");
        UART::WriteDec(owner_pid);
        UART::Write("\n");
        
        ReleaseLock();
        return buf;
    }
    
    void DestroyBuffer(SharedBuffer* buffer) {
        if (!buffer || buffer->id == 0) return;
        
        AcquireLock();
        
        // Free PMM pages (never kfree on PMM allocations)
        if (buffer->phys_addr != 0) {
            uint64_t pages = ((uint64_t)buffer->pitch * buffer->height + 4095) / 4096;
            PMM::FreePages((void*)buffer->phys_addr, pages);
            buffer->phys_addr = 0;
            buffer->kernel_addr = nullptr;
            buffer->user_addr = 0;
        }
        
        buffer->id = 0;
        buffer->owner_pid = 0;
        if (active_buffer_count > 0) active_buffer_count--;
        
        ReleaseLock();
    }

    void DestroyBuffersByPID(uint64_t pid) {
        if (pid == 0) return;
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id != 0 && buffers[i].owner_pid == pid) {
                DestroyBuffer(&buffers[i]);
            }
        }
    }
    
    SharedBuffer* GetBuffer(uint64_t id) {
        if (id == 0) return nullptr;
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id == id) return &buffers[i];
        }
        return nullptr;
    }
    
    SharedBuffer* GetBufferByPID(uint64_t pid) {
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id != 0 && buffers[i].owner_pid == pid) {
                return &buffers[i];
            }
        }
        return nullptr;
    }

    //=========================================================================
    // Frame Synchronization
    //=========================================================================
    
    void MarkReady(SharedBuffer* buffer) {
        if (!buffer) return;
        buffer->ready = true;
        buffer->consumed = false;
        // Optionally timestamp here
    }
    
    void MarkConsumed(SharedBuffer* buffer) {
        if (!buffer) return;
        buffer->consumed = true;
        buffer->ready = false; // "Ready for display" flag cleared once displayed?
                               // Actually keep 'ready' as true if it contains valid content, 
                               // but 'consumed' means compositor picked it up.
                               // Simplification: consumed=true lets app write again.
    }
    
    bool IsReady(SharedBuffer* buffer) {
        return buffer && buffer->ready;
    }
    
    bool IsConsumed(SharedBuffer* buffer) {
        return buffer && buffer->consumed;
    }

    //=========================================================================
    // Dirty Tracking
    //=========================================================================
    
    void MarkDirty(SharedBuffer* buffer, int32_t x, int32_t y, int32_t w, int32_t h) {
        if (!buffer) return;
        
        // Simple 8-rect limit. If overflow, mark full buffer dirty logic could apply,
        // but here we just stop adding or merge.
        if (buffer->dirty_count < 8) {
            buffer->dirty[buffer->dirty_count] = {x, y, w, h};
            buffer->dirty_count++;
        } else {
            // Fallback: Make one rect cover everything (union)
            // For now, just overwrite last one or ignore.
            // Let's mark whole buffer dirty by setting a full rect at index 0
            buffer->dirty[0] = {0, 0, (int32_t)buffer->width, (int32_t)buffer->height};
            buffer->dirty_count = 1;
        }
    }
    
    void ClearDirty(SharedBuffer* buffer) {
        if (!buffer) return;
        buffer->dirty_count = 0;
    }

    //=========================================================================
    // Window State
    //=========================================================================
    
    void SetPosition(SharedBuffer* buffer, int32_t x, int32_t y, 
                     uint16_t z, bool visible) {
        if (!buffer) return;
        buffer->x = x;
        buffer->y = y;
        buffer->z_order = z;
        buffer->visible = visible;
    }

    //=========================================================================
    // Composition Helpers
    //=========================================================================
    
    // Sort helper
    static void SortBuffersByZ(SharedBuffer** bufs, int count) {
        // Insertion sort is fast for small N
        for (int i = 1; i < count; i++) {
            SharedBuffer* key = bufs[i];
            int j = i - 1;
            while (j >= 0 && bufs[j]->z_order > key->z_order) {
                bufs[j + 1] = bufs[j];
                j = j - 1;
            }
            bufs[j + 1] = key;
        }
    }
    
    uint32_t GetReadyBuffers(SharedBuffer** out, uint32_t max) {
        uint32_t count = 0;
        
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            if (buffers[i].id != 0 && buffers[i].visible && buffers[i].ready) {
                if (count < max) {
                    out[count++] = &buffers[i];
                }
            }
        }
        
        SortBuffersByZ(out, count);
        return count;
    }
    
    uint32_t GetVisibleBuffers(SharedBuffer** out, uint32_t max) {
        uint32_t count = 0;
        
        for (uint32_t i = 0; i < MAX_BUFFERS; i++) {
            // Return buffers that are marked visible, even if not freshly "ready" 
            // (static content)
            if (buffers[i].id != 0 && buffers[i].visible) {
                 if (count < max) {
                    out[count++] = &buffers[i];
                }
            }
        }
        
        SortBuffersByZ(out, count);
        return count;
    }

    SharedBuffer* RegisterBuffer(uint32_t w, uint32_t h, uint64_t owner_pid, uint64_t user_ptr, uint64_t phys_addr) {
        return nullptr;
    }
    
    void* MapToUser(SharedBuffer* buf, uint64_t pid) {
        (void)pid;
        if (!buf || buf->phys_addr == 0) return nullptr;
        if (buf->user_addr != 0) return (void*)buf->user_addr;
        
        uint64_t size = (uint64_t)buf->pitch * buf->height;
        uint64_t pages = (size + 4095) / 4096;
        
        // Canonical shared surface region in process space (outside shared pml4[0]):
        // 0x7080_0000_0000 + (id * 16MB)
        uint64_t user_base = 0x708000000000ULL + (buf->id * 0x1000000ULL);
        
        for (uint64_t i = 0; i < pages; i++) {
            uint64_t u_vaddr = user_base + i * 4096;
            uint64_t p_addr  = buf->phys_addr + i * 4096;
            if (!MMU::MapPage(u_vaddr, p_addr, PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT)) {
                UART::Write("[BufferManager] MapToUser failed in MMU::MapPage\n");
                return nullptr;
            }
        }
        
        MMU::FlushTLBAll();
        buf->user_addr = user_base;
        return (void*)user_base;
    }
}
