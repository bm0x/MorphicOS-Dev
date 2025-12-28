#pragma once

// Morphic-API: Runtime Bridge for Language Interpreters
// POSIX-lite syscall interface for applications

#include <stdint.h>
#include <stddef.h>

// Syscall numbers
#define SYS_EXIT        0x00
#define SYS_WRITE       0x01
#define SYS_READ        0x02
#define SYS_OPEN        0x03
#define SYS_CLOSE       0x04
#define SYS_STAT        0x05

// Graphics syscalls (0x10-0x1F)
#define SYS_GFX_INIT    0x10
#define SYS_GFX_DRAW    0x11
#define SYS_GFX_BLIT    0x12
#define SYS_GFX_FLIP    0x13
#define SYS_GFX_CLEAR   0x14
#define SYS_GFX_TEXT    0x15

// Audio syscalls (0x20-0x2F)
#define SYS_AUDIO_INIT  0x20
#define SYS_AUDIO_PLAY  0x21
#define SYS_AUDIO_STOP  0x22
#define SYS_AUDIO_VOL   0x23

// Input syscalls (0x30-0x3F)
#define SYS_INPUT_POLL  0x30
#define SYS_INPUT_WAIT  0x31
#define SYS_INPUT_MOUSE 0x32

// Memory syscalls (0x40-0x4F)
#define SYS_ALLOC       0x40
#define SYS_FREE        0x41
#define SYS_REALLOC     0x42

// Process syscalls (0x50-0x5F)
#define SYS_YIELD       0x50
#define SYS_SLEEP       0x51
#define SYS_GETPID      0x52

// File descriptors
#define FD_STDIN   0
#define FD_STDOUT  1
#define FD_STDERR  2

// Graphics structures
struct GfxRect {
    int32_t x, y;
    uint32_t w, h;
    uint32_t color;
};

struct GfxBlit {
    int32_t x, y;
    uint32_t w, h;
    uint32_t* pixels;
};

// Input events
enum InputEventType {
    INPUT_NONE = 0,
    INPUT_KEY_DOWN,
    INPUT_KEY_UP,
    INPUT_MOUSE_MOVE,
    INPUT_MOUSE_DOWN,
    INPUT_MOUSE_UP
};

struct InputEvent {
    InputEventType type;
    union {
        struct { uint8_t scancode; char ascii; } key;
        struct { int32_t x, y; uint8_t buttons; } mouse;
    };
};

// Syscall result
struct SyscallResult {
    int64_t value;
    int32_t error;
};

namespace MorphicAPI {
    // Initialize the API layer
    void Init();
    
    // Main syscall dispatcher
    SyscallResult Syscall(uint32_t num, uint64_t arg1, uint64_t arg2, 
                          uint64_t arg3, uint64_t arg4);
    
    // File operations
    int Open(const char* path, int flags);
    int Close(int fd);
    int Read(int fd, void* buf, size_t count);
    int Write(int fd, const void* buf, size_t count);
    
    // Graphics operations
    void DrawRect(const GfxRect* rect);
    void BlitPixels(const GfxBlit* blit);
    void GfxFlip();
    void GfxClear(uint32_t color);
    void GfxText(int x, int y, const char* text, uint32_t color);
    
    // Audio operations
    int AudioPlay(const void* samples, size_t size, int channels, int rate);
    void AudioStop();
    void AudioSetVolume(int volume);
    
    // Input operations
    bool InputPoll(InputEvent* event);
    void InputWait(InputEvent* event);
    
    // Memory operations (uses UserHeap)
    void* Alloc(size_t size);
    void Free(void* ptr);
    void* Realloc(void* ptr, size_t size);
    
    // Process operations
    void Yield();
    void Sleep(uint32_t ms);
    int GetPid();
}
