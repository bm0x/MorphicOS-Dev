; Morphic OS Kernel Entry Point
; Sets up a safe stack before calling C++ kernel_main

section .bss
align 16
kernel_stack_bottom:
    resb 65536 ; 64KB Stack
kernel_stack_top:

section .text
global _start
global kernel_stack_top
extern kernel_main

_start:
    ; Disable interrupts (just in case)
    cli

    ; Update Stack Pointer to our safe BSS stack
    mov rsp, kernel_stack_top
    
    ; Clear Frame Pointer
    xor rbp, rbp

    ; RDI contains BootInfo* passed by Bootloader
    ; SysV ABI matches, so RDI is already correct for kernel_main(BootInfo* info)
    
    ; Call C++ Main
    call kernel_main

    ; Should not return, but if it does, halt
.halt:
    hlt
    jmp .halt
