[BITS 64]

global gdt_flush
global tss_load

gdt_flush:
    ; Argument rdi contains the pointer to gdtr
    lgdt [rdi]
    
    ; Reload Data Segments
    mov ax, 0x10 ; Kernel Data Segment Offset (Index 2 * 8)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Reload Code Segment via Far Jump
    mov rax, 0x08 ; Kernel Code Segment
    push rax
    
    lea rax, [rel .reload_cs]
    push rax
    
    retfq

.reload_cs:
    ret

; Load Task State Segment
tss_load:
    ; Argument: di = TSS selector
    ltr di
    ret
