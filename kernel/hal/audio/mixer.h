#pragma once

#include <stdint.h>

// Audio channel for mixing
struct AudioChannel {
    bool active;
    uint8_t* data;          // Sample data
    uint32_t size;          // Total bytes
    uint32_t position;      // Current position
    uint8_t volume;         // 0-255
    bool loop;              // Loop playback
};

#define MAX_AUDIO_CHANNELS 8

// Audio Mixer - combines multiple channels
namespace AudioMixer {
    // Initialize mixer
    void Init();
    
    // Play sample on a channel (returns channel ID or -1)
    int Play(const uint8_t* data, uint32_t size, uint8_t volume, bool loop);
    
    // Stop channel
    void StopChannel(int channel);
    
    // Stop all channels
    void StopAll();
    
    // Set channel volume
    void SetChannelVolume(int channel, uint8_t volume);
    
    // Mix all active channels into output buffer
    // Returns number of samples mixed
    uint32_t Mix(int16_t* output, uint32_t samples);
    
    // Get active channel count
    uint32_t GetActiveChannels();
}
