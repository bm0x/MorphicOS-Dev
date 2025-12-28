#include "wav.h"
#include "../../utils/std.h"
#include "../video/early_term.h"

namespace WavParser {
    WavInfo Parse(const uint8_t* data, uint32_t size) {
        WavInfo info = {false, 0, 0, 0, nullptr, 0};
        
        if (size < sizeof(WavRiffHeader) + sizeof(WavFmtChunk) + sizeof(WavDataChunk)) {
            return info;
        }
        
        // Check RIFF header
        const WavRiffHeader* riff = (const WavRiffHeader*)data;
        if (kmemcmp(riff->riff, "RIFF", 4) != 0 || 
            kmemcmp(riff->wave, "WAVE", 4) != 0) {
            EarlyTerm::Print("[WAV] Invalid RIFF header\n");
            return info;
        }
        
        // Find fmt chunk
        const uint8_t* ptr = data + sizeof(WavRiffHeader);
        const uint8_t* end = data + size;
        const WavFmtChunk* fmt = nullptr;
        const WavDataChunk* dataChunk = nullptr;
        const uint8_t* audioData = nullptr;
        
        while (ptr < end - 8) {
            uint32_t chunkSize = *(uint32_t*)(ptr + 4);
            
            if (kmemcmp(ptr, "fmt ", 4) == 0) {
                fmt = (const WavFmtChunk*)ptr;
            }
            else if (kmemcmp(ptr, "data", 4) == 0) {
                dataChunk = (const WavDataChunk*)ptr;
                audioData = ptr + sizeof(WavDataChunk);
            }
            
            ptr += 8 + chunkSize;
            // Align to word boundary
            if (chunkSize & 1) ptr++;
        }
        
        if (!fmt || !dataChunk || !audioData) {
            EarlyTerm::Print("[WAV] Missing chunks\n");
            return info;
        }
        
        // Check format
        if (fmt->audio_format != 1) {
            EarlyTerm::Print("[WAV] Not PCM format\n");
            return info;
        }
        
        info.valid = true;
        info.channels = fmt->num_channels;
        info.sample_rate = fmt->sample_rate;
        info.bits_per_sample = fmt->bits_per_sample;
        info.data = audioData;
        info.data_size = dataChunk->data_size;
        
        return info;
    }
    
    bool IsValid(const uint8_t* data, uint32_t size) {
        if (size < 12) return false;
        return kmemcmp(data, "RIFF", 4) == 0 && 
               kmemcmp(data + 8, "WAVE", 4) == 0;
    }
    
    uint32_t GetDuration(const WavInfo* info) {
        if (!info || !info->valid || info->sample_rate == 0) return 0;
        
        uint32_t samples = info->data_size / (info->channels * info->bits_per_sample / 8);
        return (samples * 1000) / info->sample_rate;
    }
}
