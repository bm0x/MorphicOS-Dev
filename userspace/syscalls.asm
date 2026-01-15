[bits 64]

global sys_get_screen_info
sys_get_screen_info:
    mov rax, 11
    syscall
    ret

global sys_get_time_ms
sys_get_time_ms:
    mov rax, 20
    syscall
    ret

global sys_get_rtc_datetime
sys_get_rtc_datetime:
    ; arg1: pointer to MorphicDateTime (rdi)
    mov rax, 55
    syscall
    ret

global sys_get_system_info
sys_get_system_info:
    ; arg1: pointer to MorphicSystemInfo (rdi)
    mov rax, 56
    syscall
    ret
    
global sys_get_event
sys_get_event:
    mov rax, 21
    syscall
    ret

global sys_alloc_backbuffer
sys_alloc_backbuffer:
    ; arg1: size (rdi)
    mov rax, 53
    syscall
    ret
    
global sys_sleep
sys_sleep:
    ; arg1: ms (rdi)
    mov rax, 13
    syscall
    ret

global sys_video_map
global sys_video_flip
global sys_video_flip_rect
global sys_input_poll

global sys_get_window_list
sys_get_window_list:
    ; arg1: buffer, arg2: max_count
    mov rax, 67
    syscall
    ret
    
global sys_update_window
sys_update_window:
    ; arg1: id, arg2: packed_pos, arg3: packed_size
    mov rax, 68
    syscall
    ret

; Syscall IDs must match kernel handler
%define SYS_VIDEO_MAP  50
%define SYS_VIDEO_FLIP 51
%define SYS_INPUT_POLL 52
%define SYS_VIDEO_FLIP_RECT 54

section .text

sys_video_map:
    mov rax, SYS_VIDEO_MAP
    syscall
    ret

sys_video_flip:
    mov rax, SYS_VIDEO_FLIP
    syscall
    ret

; arg1: rdi = backbuffer
; arg2: rsi = (x<<32)|y
; arg3: rdx = (w<<32)|h
sys_video_flip_rect:
    mov rax, SYS_VIDEO_FLIP_RECT
    syscall
    ret

sys_input_poll:
    mov rax, SYS_INPUT_POLL
    ; arg1 is already in rdi (System V ABI matches syscall ABI for arg1)
    syscall
    ret

global sys_spawn
sys_spawn:
    mov rax, 60
    syscall
    ret

global sys_debug_print
sys_debug_print:
    mov rax, 61
    syscall
    ret

global sys_create_window
sys_create_window:
    mov rax, 62
    syscall
    ret

global sys_register_compositor
sys_register_compositor:
    mov rax, 63
    syscall
    ret

global sys_map_window
sys_map_window:
    mov rax, 64
    syscall
    ret

global sys_post_message
sys_post_message:
    mov rax, 65
    syscall
    ret

global sys_compose_layers
sys_compose_layers:
    mov rax, 66
    syscall
    ret

global sys_set_keymap
sys_set_keymap:
    ; arg1: keymap_id (string ptr)
    mov rax, 70
    syscall
    ret

global sys_readdir
sys_readdir:
    ; arg1: path (rdi), arg2: entries buffer (rsi), arg3: max_entries (rdx)
    mov rax, 71
    syscall
    ret

global sys_shutdown
sys_shutdown:
    mov rax, 72
    syscall
    ret

global sys_reboot
sys_reboot:
    mov rax, 73
    syscall
    ret

global sys_read_file
sys_read_file:
    ; arg1: path (rdi), arg2: buffer (rsi), arg3: max_size (rdx)
    mov rax, 74
    syscall
    ret

global sys_yield
sys_yield:
    mov rax, 80
    syscall
    ret

global sys_list_mounts
sys_list_mounts:
    ; arg1: entries buffer (rdi), arg2: max_entries (rsi)
    mov rax, 81
    syscall
    ret
