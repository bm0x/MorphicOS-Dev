[bits 64]

global sys_get_screen_info
sys_get_screen_info:
    mov rax, 11
    syscall
    ret

global sys_video_map
global sys_video_flip
global sys_input_poll

; Syscall IDs must match kernel handler
%define SYS_VIDEO_MAP  50
%define SYS_VIDEO_FLIP 51
%define SYS_INPUT_POLL 52

section .text

sys_video_map:
    mov rax, SYS_VIDEO_MAP
    syscall
    ret

sys_video_flip:
    mov rax, SYS_VIDEO_FLIP
    syscall
    ret

sys_input_poll:
    mov rax, SYS_INPUT_POLL
    ; arg1 is already in rdi (System V ABI matches syscall ABI for arg1)
    syscall
    ret
