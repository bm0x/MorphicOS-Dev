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
    ; Use a scratch register to avoid clobbering RAX (syscall number)
    mov r9, qword [kernel_tss_ptr]  ; Load address of TSS into r9
    mov r9, qword [r9 + 4]          ; Load RSP0 (Offset 4 in packed struct)
    mov rsp, r9               ; Switch to kernel stack

    ; Preserve user callee-saved registers we will clobber
    push r12
    push r13
    push r14
    push r15

    ; Preserve syscall number and arguments into callee-saved registers
    mov r12, rax        ; save syscall number
    mov r13, rdi        ; save arg1
    mov r14, rsi        ; save arg2
    mov r15, rdx        ; save arg3

    ; Save user context (we will build a return frame later)
    push r10        ; User RSP
    push rcx        ; User RIP (saved by CPU)
    push r11        ; User RFLAGS (saved by CPU)

    ; Restore syscall arguments into call registers for C handler
    mov rdi, r12    ; syscall number -> first arg
    mov rsi, r13    ; arg1
    mov rdx, r14    ; arg2
    mov rcx, r15    ; arg3

    ; SysV ABI: stack must be 16-byte aligned before CALL.
    ; We pushed 7 qwords after switching to the kernel stack, so align here.
    sub rsp, 8
    call syscall_handler
    add rsp, 8
    ; Result in RAX
    
    ; Restore registers into temp registers for SYSRETQ
    pop r11         ; User RFLAGS
    pop rcx         ; User RIP
    pop r10         ; User RSP

    ; Restore preserved user registers
    pop r15
    pop r14
    pop r13
    pop r12

    ; Return to userland via SYSRETQ
    ; SYSRETQ uses RCX (RIP) and R11 (RFLAGS), and RSP must be user stack.
    mov rsp, r10
    o64 sysret

section .bss
align 16
kernel_syscall_stack:
    resb 16384
kernel_syscall_stack_top:
    resb 0
