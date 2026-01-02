[bits 64]
global _start
extern main

section .text
_start:
    ; Entry point for userspace applications
    ; The loader jumps here with:
    ; RSP = top of stack
    ; RDI = assets pointer (arg1)
    
    ; Align stack to 16 bytes (x86-64 System V ABI)
    and rsp, -16
    
    ; Call main(assets_ptr)
    ; RDI is already set by loader
    call main
    
    ; If main returns, loop forever (or exit syscall)
.halt:
    jmp .halt
