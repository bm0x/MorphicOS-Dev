// Morphic-API Implementation
// Runtime syscall handlers

#include "morphic_api.h"
#include "../mm/user_heap.h"
#include "../fs/vfs.h"
#include "../hal/video/early_term.h"
#include "../hal/video/graphics.h"
#include "../hal/video/compositor.h"
#include "../hal/input/keymap.h"
#include "../hal/audio/mixer.h"
#include "../utils/std.h"

namespace MorphicAPI {
    // File descriptor table (simple implementation)
    #define MAX_FDS 16
    static VFSNode* fdTable[MAX_FDS] = {nullptr};
    
    void Init() {
        // FD 0, 1, 2 are reserved for stdin/stdout/stderr
        for (int i = 0; i < MAX_FDS; i++) {
            fdTable[i] = nullptr;
        }
        EarlyTerm::Print("[MorphicAPI] Initialized.\n");
    }
    
    SyscallResult Syscall(uint32_t num, uint64_t arg1, uint64_t arg2, 
                          uint64_t arg3, uint64_t arg4) {
        SyscallResult result = {0, 0};
        
        switch (num) {
            case SYS_EXIT:
                // TODO: Terminate process
                break;
                
            case SYS_WRITE:
                result.value = Write((int)arg1, (void*)arg2, (size_t)arg3);
                break;
                
            case SYS_READ:
                result.value = Read((int)arg1, (void*)arg2, (size_t)arg3);
                break;
                
            case SYS_OPEN:
                result.value = Open((const char*)arg1, (int)arg2);
                break;
                
            case SYS_CLOSE:
                result.value = Close((int)arg1);
                break;
                
            case SYS_GFX_DRAW:
                DrawRect((const GfxRect*)arg1);
                break;
                
            case SYS_GFX_BLIT:
                BlitPixels((const struct GfxBlit*)arg1);
                break;

                
            case SYS_GFX_FLIP:
                GfxFlip();
                break;
                
            case SYS_GFX_CLEAR:
                GfxClear((uint32_t)arg1);
                break;
                
            case SYS_AUDIO_PLAY:
                result.value = AudioPlay((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4);
                break;
                
            case SYS_AUDIO_STOP:
                AudioStop();
                break;
                
            case SYS_INPUT_POLL:
                result.value = InputPoll((InputEvent*)arg1) ? 1 : 0;
                break;
                
            case SYS_ALLOC:
                result.value = (uint64_t)Alloc((size_t)arg1);
                break;
                
            case SYS_FREE:
                Free((void*)arg1);
                break;
                
            case SYS_YIELD:
                Yield();
                break;
                
            case SYS_SLEEP:
                Sleep((uint32_t)arg1);
                break;
                
            default:
                result.error = -1;  // Unknown syscall
                break;
        }
        
        return result;
    }
    
    int Open(const char* path, int flags) {
        (void)flags;
        
        VFSNode* node = VFS::Open(path);
        if (!node) return -1;
        
        // Find free FD
        for (int i = 3; i < MAX_FDS; i++) {
            if (!fdTable[i]) {
                fdTable[i] = node;
                return i;
            }
        }
        
        return -1;  // No free FDs
    }
    
    int Close(int fd) {
        if (fd < 0 || fd >= MAX_FDS) return -1;
        if (!fdTable[fd]) return -1;
        
        VFS::Close(fdTable[fd]);
        fdTable[fd] = nullptr;
        return 0;
    }
    
    int Read(int fd, void* buf, size_t count) {
        if (fd < 0 || fd >= MAX_FDS) return -1;
        
        // stdin
        if (fd == FD_STDIN) {
            // TODO: Read from keyboard buffer
            return 0;
        }
        
        VFSNode* node = fdTable[fd];
        if (!node) return -1;
        
        return VFS::Read(node, 0, count, (uint8_t*)buf);
    }
    
    int Write(int fd, const void* buf, size_t count) {
        // stdout/stderr go to terminal
        if (fd == FD_STDOUT || fd == FD_STDERR) {
            for (size_t i = 0; i < count; i++) {
                EarlyTerm::PutChar(((const char*)buf)[i]);
            }
            return count;
        }
        
        if (fd < 0 || fd >= MAX_FDS) return -1;
        
        VFSNode* node = fdTable[fd];
        if (!node) return -1;
        
        return VFS::Write(node, 0, count, (uint8_t*)buf);
    }
    
    void DrawRect(const GfxRect* rect) {
        if (!rect) return;
        Graphics::DrawRect(rect->x, rect->y, rect->w, rect->h, rect->color);
    }
    
    void BlitPixels(const struct GfxBlit* blit) {
        if (!blit || !blit->pixels) return;
        // Copy pixels to backbuffer
        for (uint32_t y = 0; y < blit->h; y++) {
            for (uint32_t x = 0; x < blit->w; x++) {
                Graphics::PutPixel(blit->x + x, blit->y + y, 
                                   blit->pixels[y * blit->w + x]);
            }
        }
    }
    
    void GfxFlip() {
        Graphics::Flip();
    }
    
    void GfxClear(uint32_t color) {
        Graphics::FillRect(0, 0, Graphics::GetWidth(), Graphics::GetHeight(), color);
    }
    
    void GfxText(int x, int y, const char* text, uint32_t color) {
        // TODO: Use font renderer
        (void)x; (void)y; (void)text; (void)color;
    }
    
    int AudioPlay(const void* samples, size_t size, int channels, int rate) {
        (void)samples; (void)size; (void)channels; (void)rate;
        // TODO: Queue audio samples
        return 0;
    }
    
    void AudioStop() {
        // TODO: Stop audio playback
    }
    
    void AudioSetVolume(int volume) {
        // TODO: Set master volume when mixer supports it
        (void)volume;
    }
    
    bool InputPoll(InputEvent* event) {
        if (!event) return false;
        
        // TODO: Integrate with keyboard driver properly
        // For now, return no events
        event->type = INPUT_NONE;
        return false;
    }
    
    void InputWait(InputEvent* event) {
        while (!InputPoll(event)) {
            Yield();
        }
    }
    
    void* Alloc(size_t size) {
        return UserHeap::Allocate(size, MEM_PRIORITY_NORMAL);
    }
    
    void Free(void* ptr) {
        UserHeap::Free(ptr);
    }
    
    void* Realloc(void* ptr, size_t size) {
        return UserHeap::Realloc(ptr, size);
    }
    
    void Yield() {
        // TODO: Scheduler yield
        __asm__ volatile("pause");
    }
    
    void Sleep(uint32_t ms) {
        // Simple busy wait (TODO: use PIT)
        for (volatile uint32_t i = 0; i < ms * 1000; i++);
    }
    
    int GetPid() {
        return 0;  // TODO: Return actual PID
    }
}
