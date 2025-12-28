[BITS 64]

global syscall_entry

extern syscall_handler

; SYSCALL entry point
; CPU saves: RIP -> RCX, RFLAGS -> R11
; User passes: RAX=syscall#, RDI=arg1, RSI=arg2, RDX=arg3
syscall_entry:
    ; Swap to kernel stack (we need TSS.rsp0)
    ; For now, use a simple kernel stack
    ; In production, would get from TSS
    
    ; Save user stack
    mov r10, rsp
    
    ; Switch to temporary kernel stack (simple approach)
    mov rsp, kernel_syscall_stack_top
    
    ; Save registers
    push r10        ; User RSP
    push rcx        ; User RIP (saved by CPU)
    push r11        ; User RFLAGS (saved by CPU)
    
    ; Call C handler
    ; Arguments already in: RDI=arg1, RSI=arg2, RDX=arg3
    ; Move syscall number to first argument
    mov rcx, rdx    ; arg3
    mov rdx, rsi    ; arg2
    mov rsi, rdi    ; arg1
    mov rdi, rax    ; syscall number
    
    call syscall_handler
    ; Result in RAX
    
    ; Restore registers
    pop r11         ; User RFLAGS
    pop rcx         ; User RIP
    pop r10         ; User RSP
    
    ; Restore user stack
    mov rsp, r10
    
    ; Return to user mode
    o64 sysret

section .bss
align 16
kernel_syscall_stack:
    resb 4096
kernel_syscall_stack_top:
