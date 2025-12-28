[BITS 64]

global isr_stub_table
extern GenericExceptionHandler

%macro ISR_NOERRCODE 1
    global isr%1
    isr%1:
        cli
        push 0 ; Push dummy error code
        push %1 ; Push interrupt number
        jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
    global isr%1
    isr%1:
        cli
        push %1 ; Push interrupt number
        jmp isr_common_stub
%endmacro

; Define Stubs
ISR_NOERRCODE 0  ; Div Zero
ISR_NOERRCODE 1  ; Debug
ISR_NOERRCODE 2  ; NMI
ISR_NOERRCODE 3  ; Breakpoint
ISR_NOERRCODE 4  ; Overflow
ISR_NOERRCODE 5  ; Bound Range
ISR_NOERRCODE 6  ; Invalid Opcode
ISR_NOERRCODE 7  ; Device Not Available
ISR_ERRCODE   8  ; Double Fault
ISR_NOERRCODE 9  ; Coprocessor Segment Overrun
ISR_ERRCODE   10 ; Invalid TSS
ISR_ERRCODE   11 ; Segment Not Present
ISR_ERRCODE   12 ; Stack Segment Fault
ISR_ERRCODE   13 ; General Protection
ISR_ERRCODE   14 ; Page Fault
ISR_NOERRCODE 15 ; Reserved
ISR_NOERRCODE 16 ; x87 Float
ISR_ERRCODE   17 ; Alignment Check
ISR_NOERRCODE 18 ; Machine Check
ISR_NOERRCODE 19 ; SIMD Float
ISR_NOERRCODE 20 ; Virtualization
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30 ; Security Exception
ISR_NOERRCODE 31

; ... IRQs later ...

isr_common_stub:
    ; Save Registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Pass pointer to stack structure as argument
    mov rdi, rsp 
    
    ; Call C++ Handler
    call GenericExceptionHandler
    
    ; Restore Registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Remove Error Code and Int Number
    add rsp, 16 
    
    iretq

; Helper to load IDT
global idt_flush
idt_flush:
    lidt [rdi]
    ret

; IRQ Stubs (starting at 32)
%macro IRQ 2
    global irq%1
    irq%1:
        cli
        push 0
        push %2 ; Push Interrupt Number (32 + IRQ)
        jmp irq_common_stub
%endmacro

IRQ   0, 32
IRQ   1, 33
IRQ   2, 34
IRQ   3, 35
IRQ   4, 36
IRQ   5, 37
IRQ   6, 38
IRQ   7, 39
IRQ   8, 40
IRQ   9, 41
IRQ  10, 42
IRQ  11, 43
IRQ  12, 44
IRQ  13, 45
IRQ  14, 46
IRQ  15, 47

extern GenericIRQHandler

irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    
    call GenericIRQHandler
    
    ; Context Switch Magic
    mov rsp, rax ; Switch Stack to whatever Scheduler returned

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16
    iretq

; Isr Stub Table update (must export IRQs too)
global isr_stub_table
section .data
isr_stub_table:
    dq isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dq isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dq isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dq isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    ; IRQs
    dq irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    dq irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

