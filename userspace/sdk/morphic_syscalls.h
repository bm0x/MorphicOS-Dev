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
uint64_t sys_video_flip(void* backbuffer);
uint64_t sys_video_flip_rect(void* backbuffer, uint64_t xy, uint64_t wh);
uint64_t sys_input_poll(void* out /* optional */);
int      sys_spawn(const char* path);
void     sys_debug_print(const char* msg);

#ifdef __cplusplus
}
#endif
