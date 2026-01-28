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
    ; Preserve syscall number and arguments into callee-saved registers
    ; RAX = syscall number, RDI = arg1, RSI = arg2, RDX = arg3
    mov r12, rax        ; save syscall number
    mov r13, rdi        ; save arg1
    mov r14, rsi        ; save arg2
    mov r15, rdx        ; save arg3

    ; Switch to Thread-Specific Kernel Stack (from TSS.RSP0)
    ; Use a temporary register instead of RSP to avoid fragile pointer-on-stack hacks
    mov rax, qword [kernel_tss_ptr] ; Load address of TSS into rax
    mov rax, qword [rax + 4]        ; Load RSP0 (Offset 4 in packed struct)
    mov rsp, rax              ; Switch to kernel stack

    ; Save user context (we will build IRETQ frame later)
    push r10        ; User RSP
    push rcx        ; User RIP (saved by CPU)
    push r11        ; User RFLAGS (saved by CPU)

    ; Restore syscall arguments into call registers for C handler
    mov rdi, r12    ; syscall number -> first arg
    mov rsi, r13    ; arg1
    mov rdx, r14    ; arg2
    mov rcx, r15    ; arg3

    call syscall_handler
    ; Result in RAX
    
    ; Restore registers into temp registers (we will build an IRETQ frame)
    pop r11         ; User RFLAGS
    pop rcx         ; User RIP
    pop r10         ; User RSP

    ; CRITICAL: Disable interrupts before returning to userland
    cli

    ; Prepare IRETQ frame on stack: SS, RSP, RFLAGS, CS, RIP
    ; Use selectors matching JumpToUser: User SS = 0x18|3 = 0x1B, User CS = 0x20|3 = 0x23
    push 0x1B           ; User SS (push immediate to avoid clobbering rax)
    push r10            ; User RSP
    push r11            ; User RFLAGS
    push 0x23           ; User CS (push immediate)
    push rcx            ; User RIP
    iretq

section .bss
align 16
kernel_syscall_stack:
    resb 16384
kernel_syscall_stack_top:
