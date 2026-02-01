#include "focus_manager.h"
#include "../serial/uart.h"
#include "../video/graphics.h"
#include "../drm/buffer_manager.h"

using namespace BufferManager;

namespace FocusManager {

    static FocusState state;

    void Init() {
        state.focused_pid = 0;
        state.focused_buffer_id = 0;
        state.mouse_x = Graphics::GetWidth() / 2;
        state.mouse_y = Graphics::GetHeight() / 2;
        state.dragging = false;
        state.mouse_owner_pid = 0;
        UART::Write("[FocusManager] Initialized\n");
    }

    bool HandleMouse(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
        // Update position
        state.mouse_x += delta_x;
        state.mouse_y += delta_y;
        
        // Clamp to screen
        int32_t w = (int32_t)Graphics::GetWidth();
        int32_t h = (int32_t)Graphics::GetHeight();
        
        if (state.mouse_x < 0) state.mouse_x = 0;
        if (state.mouse_y < 0) state.mouse_y = 0;
        if (state.mouse_x >= w) state.mouse_x = w - 1;
        if (state.mouse_y >= h) state.mouse_y = h - 1;

        // Hit testing
        SharedBuffer* bufs[64];
        uint32_t count = BufferManager::GetVisibleBuffers(bufs, 64);
        
        // Iterate back-to-front (highest Z first)
        // Buffers are sorted by Z ascending, so last is top
        int hit_index = -1;
        
        for (int i = (int)count - 1; i >= 0; i--) {
            SharedBuffer* b = bufs[i];
            if (state.mouse_x >= b->x && state.mouse_x < b->x + (int32_t)b->width &&
                state.mouse_y >= b->y && state.mouse_y < b->y + (int32_t)b->height) {
                hit_index = i;
                break;
            }
        }
        
        if (hit_index >= 0) {
            SharedBuffer* hit = bufs[hit_index];
            state.mouse_owner_pid = hit->owner_pid;
            
            // Click to focus
            if ((buttons & 1) && state.focused_pid != hit->owner_pid) {
                SetFocus(hit->owner_pid, hit->id);
            }
        } else {
            state.mouse_owner_pid = 0; // Desktop/Background
        }
        
        // Update cursor visual (via DRM/Graphics)
        // For now, Desktop draws cursor, but we updated the coordinates here.
        // How does Desktop know the new coordinates? 
        // Desktop reads mouse state via syscall SYS_GET_MOUSE_STATE.
        // We need to ensure SYS_GET_MOUSE_STATE reads *this* state.
        
        return true;
    }

    void SetFocus(uint64_t pid, uint64_t buffer_id) {
        if (state.focused_pid == pid && state.focused_buffer_id == buffer_id) return;
        
        state.focused_pid = pid;
        state.focused_buffer_id = buffer_id;
        
        UART::Write("[Focus] Switch to PID: ");
        UART::WriteDec(pid);
        UART::Write("\n");
        
        // Bring to front (Modify Z-order)
        SharedBuffer* buf = BufferManager::GetBuffer(buffer_id);
        if (buf) {
            // Simple logic: make it Z=100, demote others?
            // Real logic requires Z-management in BufferManager
            buf->z_order = 100; // Force top
        }
    }

    FocusState GetState() {
        return state;
    }
}
