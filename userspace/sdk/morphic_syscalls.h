#pragma once
#include <stdint.h>

// C prototypes for the syscall stubs implemented in userspace/syscalls.asm.
// Include this from MPK apps to avoid repeating extern declarations.

#ifdef __cplusplus
extern "C" {
#endif

uint64_t sys_get_screen_info();
uint64_t sys_get_time_ms();
int      sys_get_rtc_datetime(void* out_datetime /* MorphicDateTime* */);
int      sys_get_system_info(void* out_info /* MorphicSystemInfo* */);
int      sys_get_event(void* ev /* OSEvent* */);
uint64_t sys_alloc_backbuffer(uint64_t size);
void     sys_sleep(uint32_t ms);
void*    sys_video_map();
void*    sys_create_window(uint32_t w, uint32_t h, uint32_t flags);
uint64_t sys_video_flip(void* backbuffer);
uint64_t sys_video_flip_rect(void* backbuffer, uint64_t xy, uint64_t wh);
uint64_t sys_input_poll(void* out /* optional */);
int      sys_spawn(const char* path);
void     sys_debug_print(const char* msg);
void     sys_register_compositor();
void*    sys_map_window(uint64_t windowId);
void     sys_post_message(uint64_t targetPid, void* event);
// Compositor: Overlay APP_WINDOW layers on backbuffer (for Desktop use)
void     sys_compose_layers();

struct WindowInfo {
    uint64_t id;
    uint32_t x, y, w, h;
    uint32_t flags;
    char title[32];
    uint64_t pid;
};

uint32_t sys_get_window_list(void* buffer, uint32_t max_count);
// packed_pos = (x << 32) | y
// packed_size = (w << 32) | h
uint64_t sys_update_window(uint64_t id, uint64_t packed_pos, uint64_t packed_size);

// Keymap
int sys_set_keymap(const char* id);

// Directory listing entry
struct DirEntry {
    char name[64];
    uint32_t type;  // 0=file, 1=directory
    uint32_t size;
};

// List directory contents, returns count of entries written
int sys_readdir(const char* path, DirEntry* entries, int max_entries);

// Power controls
void sys_shutdown();
void sys_reboot();

// File operations
int sys_read_file(const char* path, void* buffer, int max_size);
void sys_yield();

// Mount listing
struct MountEntry {
    char path[32];
    char fstype[16];
};
int sys_list_mounts(MountEntry* entries, int max_entries);

// Direct mouse state read for quick diagnostics
uint64_t sys_get_mouse_state();

#ifdef __cplusplus
}
#endif

