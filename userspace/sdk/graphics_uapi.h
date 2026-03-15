#pragma once

#include "../../shared/graphics_uapi_shared.h"
#include "morphic_syscalls.h"

namespace MorphicGfx {

enum AtomicFlags : uint32_t {
    ATOMIC_WAIT_VSYNC = (1u << 0),
    ATOMIC_FULL_UPDATE = (1u << 1),
};

inline int QueryCaps(GraphicsUapiCaps* out_caps) {
    return sys_drm_get_caps(out_caps);
}

inline int PollEvent(GraphicsUapiEvent* out_event) {
    return sys_drm_poll_event(out_event);
}

inline uint64_t CreateBuffer(uint32_t w, uint32_t h) {
    return sys_drm_create_buffer(w, h);
}

inline int DestroyBuffer(uint64_t id) {
    return sys_drm_destroy_buffer(id);
}

inline void* MapBuffer(uint64_t id) {
    return sys_drm_map_buffer(id);
}

inline int MarkReady(uint64_t id) {
    return sys_drm_mark_ready(id);
}

inline uint32_t PackPos16(int16_t x, int16_t y) {
    return ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
}

inline uint32_t PackSize16(uint16_t w, uint16_t h) {
    return ((uint32_t)h << 16) | (uint32_t)w;
}

inline int MarkDirtyRect(uint64_t id, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    return sys_drm_mark_dirty_rect(id, PackPos16(x, y), PackSize16(w, h));
}

inline uint64_t Present(uint32_t flags) {
    return sys_drm_present(flags);
}

inline int MarkCompositorDirty(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    return sys_drm_mark_compositor_dirty(PackPos16(x, y), PackSize16(w, h));
}

inline int AtomicTest(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t flags) {
    return sys_drm_atomic_test(PackPos16(x, y), PackSize16(w, h), flags);
}

inline int AtomicCommit(int16_t x, int16_t y, uint16_t w, uint16_t h, uint32_t flags) {
    return sys_drm_atomic_commit(PackPos16(x, y), PackSize16(w, h), flags);
}

inline int AtomicTestFull(bool wait_vsync) {
    uint32_t flags = ATOMIC_FULL_UPDATE | (wait_vsync ? ATOMIC_WAIT_VSYNC : 0u);
    return sys_drm_atomic_test(0, 0, flags);
}

inline int AtomicCommitFull(bool wait_vsync) {
    uint32_t flags = ATOMIC_FULL_UPDATE | (wait_vsync ? ATOMIC_WAIT_VSYNC : 0u);
    return sys_drm_atomic_commit(0, 0, flags);
}

}
