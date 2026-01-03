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
