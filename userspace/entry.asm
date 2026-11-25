[bits 64]
global _start
extern main
extern user_early_trace_start
extern user_early_trace_end

section .text.entry
_start:
    ; Entry point for userspace applications
    ; The loader jumps here with:
    ; RSP = top of stack
    
    ; Align stack to 16 bytes (x86-64 System V ABI)
    and rsp, -16
    
    ; Preserve RDI (assets_ptr passed by kernel loader) across early trace call
    push rdi
    sub rsp, 8
    call user_early_trace_start
    add rsp, 8
    pop rdi

    ; Call main(assets_ptr)
    call main

    ; Preserve main() return code across end trace
    push rax
    sub rsp, 8
    call user_early_trace_end
    add rsp, 8
    pop rdi

    ; Cleanly terminate process via SYS_EXIT (RAX = 0, RDI = exit_code)
    xor rax, rax
    syscall

.halt:
    jmp .halt
