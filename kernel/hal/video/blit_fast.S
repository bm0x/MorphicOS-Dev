# SIMD Optimized Memory Blit for Morphic OS Compositor
# Uses REP MOVSQ for fast 64-bit aligned memory copy
# This is critical for framebuffer operations

.section .text
.global blit_fast_64
.global blit_fast_32
.global memset_fast_64

# void blit_fast_64(void* dest, void* src, size_t qwords)
# RDI = destination pointer
# RSI = source pointer  
# RDX = count (number of 64-bit qwords to copy)
# Copies 8 bytes per iteration, optimal for framebuffer rows
blit_fast_64:
    push %rbp
    mov %rsp, %rbp
    
    mov %rdx, %rcx          # RCX = count for REP
    cld                      # Clear direction flag (forward copy)
    rep movsq               # Copy RCX qwords from RSI to RDI
    
    pop %rbp
    ret

# void blit_fast_32(void* dest, void* src, size_t dwords)
# RDI = destination pointer
# RSI = source pointer
# RDX = count (number of 32-bit dwords to copy)
# Copies 4 bytes per iteration, for pixel-aligned copies
blit_fast_32:
    push %rbp
    mov %rsp, %rbp
    
    mov %rdx, %rcx          # RCX = count for REP
    cld                      # Clear direction flag
    rep movsl               # Copy RCX dwords (32-bit) from RSI to RDI
    
    pop %rbp
    ret

# void memset_fast_64(void* dest, uint64_t value, size_t qwords)
# RDI = destination pointer
# RSI = 64-bit value to set
# RDX = count (number of qwords)
# Fast memory fill for clearing framebuffer regions
memset_fast_64:
    push %rbp
    mov %rsp, %rbp
    
    mov %rsi, %rax          # RAX = value for REP STOSQ
    mov %rdx, %rcx          # RCX = count
    cld                      # Clear direction flag
    rep stosq               # Store RAX to RDI, RCX times
    
    pop %rbp
    ret

# void memset_fast_32(void* dest, uint32_t value, size_t dwords)
# RDI = destination pointer
# ESI = 32-bit value to set
# RDX = count (number of dwords = pixels for ARGB32)
.global memset_fast_32
memset_fast_32:
    push %rbp
    mov %rsp, %rbp
    
    mov %esi, %eax          # EAX = value for REP STOSL
    mov %rdx, %rcx          # RCX = count
    cld
    rep stosl               # Store EAX to RDI, RCX times
    
    pop %rbp
    ret
