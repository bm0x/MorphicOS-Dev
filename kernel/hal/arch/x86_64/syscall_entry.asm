[BITS 64]

global syscall_entry

extern kernel_tss_ptr
extern syscall_handler

; SYSCALL entry point
; CPU saves: RIP -> RCX, RFLAGS -> R11
; User passes: RAX=syscall#, RDI=arg1, RSI=arg2, RDX=arg3
syscall_entry:
    ; Critical: Disable interrupts immediately
    cli

    ; Save user stack
    mov r10, rsp
    
    ; Switch to Thread-Specific Kernel Stack (from TSS.RSP0)
    ; This ensures we don't corrupt a global stack if interrupted and switched.
    mov rsp, [kernel_tss_ptr] ; Load address of TSS
    mov rsp, [rsp + 4]        ; Load RSP0 (Offset 4 in packed struct)
    
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
    
    ; CRITICAL: Disable interrupts before switching to user stack.
    ; If an interrupt fires while we are in Ring 0 but using User Stack,
    ; the scheduler will save a User Stack Pointer as the Task's Kernel Memory.
    ; This corrupts TSS.RSP0 and leads to Kernel Panics (Running on User Stack).
    cli

    ; Restore user stack
    mov rsp, r10
    
    ; Return to user mode
    o64 sysret

section .bss
align 16
kernel_syscall_stack:
    resb 4096
kernel_syscall_stack_top:
