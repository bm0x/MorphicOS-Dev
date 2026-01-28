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
    
    ; Early userspace trace: indicate app started
    call user_early_trace_start

    ; Call main(assets_ptr)
    ; RDI is already set by loader
    call main

    ; Early userspace trace: main returned
    call user_early_trace_end
    
    ; If main returns, loop forever (or exit syscall)
.halt:
    jmp .halt
