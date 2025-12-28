#pragma once

#include <stdint.h>

// WAV file format structures
#pragma pack(push, 1)
struct WavRiffHeader {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
};

struct WavFmtChunk {
    char fmt[4];            // "fmt "
    uint32_t chunk_size;    // 16 for PCM
    uint16_t audio_format;  // 1 = PCM
    uint16_t num_channels;  // 1 = mono, 2 = stereo
    uint32_t sample_rate;   // e.g., 44100
    uint32_t byte_rate;     // sample_rate * channels * bits/8
    uint16_t block_align;   // channels * bits/8
    uint16_t bits_per_sample;
};

struct WavDataChunk {
    char data[4];           // "data"
    uint32_t data_size;     // Size of audio data
};
#pragma pack(pop)

// Parsed WAV info
struct WavInfo {
    bool valid;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    const uint8_t* data;
    uint32_t data_size;
};

// WAV Parser
namespace WavParser {
    // Parse WAV data from memory
    WavInfo Parse(const uint8_t* data, uint32_t size);
    
    // Validate WAV header
    bool IsValid(const uint8_t* data, uint32_t size);
    
    // Get audio duration in milliseconds
    uint32_t GetDuration(const WavInfo* info);
}
