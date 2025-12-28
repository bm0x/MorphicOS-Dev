#include "mixer.h"
#include "../video/early_term.h"
#include "../../utils/std.h"

namespace AudioMixer {
    static AudioChannel channels[MAX_AUDIO_CHANNELS];
    static uint32_t activeCount = 0;
    
    void Init() {
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
            channels[i].active = false;
            channels[i].data = nullptr;
            channels[i].size = 0;
            channels[i].position = 0;
            channels[i].volume = 255;
            channels[i].loop = false;
        }
        activeCount = 0;
        EarlyTerm::Print("[AudioMixer] Initialized with ");
        EarlyTerm::PrintDec(MAX_AUDIO_CHANNELS);
        EarlyTerm::Print(" channels.\n");
    }
    
    int Play(const uint8_t* data, uint32_t size, uint8_t volume, bool loop) {
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
            if (!channels[i].active) {
                channels[i].active = true;
                channels[i].data = (uint8_t*)data;
                channels[i].size = size;
                channels[i].position = 0;
                channels[i].volume = volume;
                channels[i].loop = loop;
                activeCount++;
                return i;
            }
        }
        return -1;  // No free channel
    }
    
    void StopChannel(int channel) {
        if (channel >= 0 && channel < MAX_AUDIO_CHANNELS) {
            if (channels[channel].active) {
                channels[channel].active = false;
                activeCount--;
            }
        }
    }
    
    void StopAll() {
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
            channels[i].active = false;
        }
        activeCount = 0;
    }
    
    void SetChannelVolume(int channel, uint8_t volume) {
        if (channel >= 0 && channel < MAX_AUDIO_CHANNELS) {
            channels[channel].volume = volume;
        }
    }
    
    // Mix algorithm with saturation
    uint32_t Mix(int16_t* output, uint32_t samples) {
        // Clear output buffer
        for (uint32_t i = 0; i < samples; i++) {
            output[i] = 0;
        }
        
        // Mix each active channel
        for (int ch = 0; ch < MAX_AUDIO_CHANNELS; ch++) {
            AudioChannel& c = channels[ch];
            if (!c.active || !c.data) continue;
            
            for (uint32_t i = 0; i < samples; i++) {
                if (c.position >= c.size) {
                    if (c.loop) {
                        c.position = 0;
                    } else {
                        c.active = false;
                        activeCount--;
                        break;
                    }
                }
                
                // Get sample (assuming 16-bit signed)
                int16_t sample = (int16_t)(c.data[c.position] | 
                                          (c.data[c.position + 1] << 8));
                c.position += 2;
                
                // Apply volume
                sample = (sample * c.volume) / 255;
                
                // Mix with saturation
                int32_t mixed = output[i] + sample;
                if (mixed > 32767) mixed = 32767;
                if (mixed < -32768) mixed = -32768;
                output[i] = (int16_t)mixed;
            }
        }
        
        return samples;
    }
    
    uint32_t GetActiveChannels() {
        return activeCount;
    }
}
