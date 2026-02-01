#pragma once

#include <stdint.h>
#include "../drm/buffer_manager.h"

namespace FocusManager {

    struct FocusState {
        uint64_t focused_pid;
        uint64_t focused_buffer_id;
        
        uint64_t mouse_owner_pid;
        int32_t  mouse_x, mouse_y;
        
        // Dragging state
        bool     dragging;
        uint64_t drag_buffer_id;
        int32_t  drag_offset_x, drag_offset_y;
    };

    /**
     * Initialize Focus Manager
     */
    void Init();

    /**
     * Handle generic mouse event
     * Routes logic to appropriate window/buffer
     * @return true if event was consumed
     */
    bool HandleMouse(int32_t delta_x, int32_t delta_y, uint8_t buttons);
    
    /**
     * Set focus to specific window/process
     */
    void SetFocus(uint64_t pid, uint64_t buffer_id);
    
    /**
     * Get current focus state
     */
    FocusState GetState();

}
