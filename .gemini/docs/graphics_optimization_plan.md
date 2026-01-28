# Morphic OS Graphics Optimization Plan
## Target: Stable 60 FPS with Professional Feel

### Current Architecture Problems

1. **Triple Memory Copy Chain**:
   ```
   Userspace backBuffer → frontBuffer (Flush)
   frontBuffer → Kernel backbuffer (sys_video_flip)
   Kernel backbuffer → VRAM (Graphics::Flip)
   ```
   This is ~3x the memory bandwidth needed.

2. **VSync Implementation**: Pure polling with no interrupt support

3. **Dirty Rect Fragmentation**: 3 separate systems that don't communicate

---

## New Simplified Architecture

### 1. Single Shared Buffer Model
- Userspace draws DIRECTLY to kernel-mapped shared buffer
- Eliminate intermediate copies entirely
- One `sys_video_present()` syscall triggers VSync + VRAM flip

### 2. Hardware VSync with Timer Fallback
- Use VGA port 0x3DA for detection only
- If hardware VSync fails, use PIT timer for ~16.67ms frame pacing
- Never busy-wait for more than 2ms

### 3. Unified Dirty Rect System
- Single dirty rect tracker in kernel
- Userspace marks regions via syscall
- Kernel only copies dirty regions to VRAM

---

## Implementation Phases

### Phase 1: Simplify VSync (vsync.cpp)
- Remove excessive timeouts
- Add clean timer-based fallback
- Use `pause` instruction efficiently

### Phase 2: Optimize Memory Copies (graphics.cpp, compositor.cpp)
- Use SIMD (REP MOVSD) for all buffer copies
- Reduce copy count from 3 to 1

### Phase 3: Unify Dirty Rect System
- Remove duplicate implementations
- Single source of truth in Graphics namespace

### Phase 4: Streamline Frame Pipeline
- Remove redundant `sys_compose_layers()` calls
- Batch compositor operations

---

## Expected Results
- Frame time: <16ms consistently
- CPU usage during idle: <5%
- No visible tearing or stuttering
- Natural, responsive mouse movement
