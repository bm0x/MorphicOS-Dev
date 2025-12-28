#pragma once

#include <stdint.h>

// Audio sample formats
enum class AudioFormat {
    PCM_8BIT_MONO,
    PCM_8BIT_STEREO,
    PCM_16BIT_MONO,
    PCM_16BIT_STEREO
};

// Audio device capabilities
struct AudioDeviceInfo {
    char name[32];
    uint32_t sample_rate;       // e.g., 44100, 48000
    uint8_t channels;           // 1 = mono, 2 = stereo
    uint8_t bits_per_sample;    // 8 or 16
    uint32_t buffer_size;       // Ring buffer size
    bool supports_dma;
};

// Audio device interface (HAL abstraction)
struct IAudioDevice {
    char name[32];
    
    // Function pointers for HAL pattern
    void (*init)();
    void (*play)();
    void (*pause)();
    void (*stop)();
    void (*set_volume)(uint8_t volume);  // 0-255
    uint32_t (*write)(const uint8_t* data, uint32_t size);
    bool (*is_playing)();
};

// Audio HAL - Central audio management
namespace Audio {
    // Initialize audio subsystem
    void Init();
    
    // Register audio device
    void RegisterDevice(IAudioDevice* device);
    
    // Get primary audio device
    IAudioDevice* GetPrimaryDevice();
    
    // Playback control
    void Play();
    void Pause();
    void Stop();
    void SetVolume(uint8_t volume);
    
    // Write audio data to buffer
    uint32_t Write(const uint8_t* data, uint32_t size);
    
    // Status
    bool IsPlaying();
    uint8_t GetVolume();
    
    // Play a sound file from VFS
    bool PlayFile(const char* path);
    
    // Play a simple beep (PC Speaker fallback)
    void Beep(uint32_t frequency, uint32_t duration_ms);
}
