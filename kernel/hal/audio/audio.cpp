#include "audio_device.h"
#include "ring_buffer.h"
#include "../video/early_term.h"
#include "../arch/x86_64/io.h"

namespace Audio {
    static IAudioDevice* devices[4];
    static uint32_t deviceCount = 0;
    static IAudioDevice* primaryDevice = nullptr;
    static uint8_t masterVolume = 200;
    static bool playing = false;
    
    // Audio buffer
    static StandardAudioBuffer audioBuffer;
    
    void Init() {
        deviceCount = 0;
        primaryDevice = nullptr;
        masterVolume = 200;
        playing = false;
        audioBuffer.Init();
        
        EarlyTerm::Print("[Audio] HAL initialized.\n");
    }
    
    void RegisterDevice(IAudioDevice* device) {
        if (deviceCount < 4 && device) {
            devices[deviceCount++] = device;
            if (!primaryDevice) {
                primaryDevice = device;
                device->init();
                EarlyTerm::Print("[Audio] Primary device: ");
                EarlyTerm::Print(device->name);
                EarlyTerm::Print("\n");
            }
        }
    }
    
    IAudioDevice* GetPrimaryDevice() {
        return primaryDevice;
    }
    
    void Play() {
        if (primaryDevice && primaryDevice->play) {
            primaryDevice->play();
            playing = true;
        }
    }
    
    void Pause() {
        if (primaryDevice && primaryDevice->pause) {
            primaryDevice->pause();
            playing = false;
        }
    }
    
    void Stop() {
        if (primaryDevice && primaryDevice->stop) {
            primaryDevice->stop();
            playing = false;
            audioBuffer.Clear();
        }
    }
    
    void SetVolume(uint8_t volume) {
        masterVolume = volume;
        if (primaryDevice && primaryDevice->set_volume) {
            primaryDevice->set_volume(volume);
        }
    }
    
    uint32_t Write(const uint8_t* data, uint32_t size) {
        return audioBuffer.Write(data, size);
    }
    
    bool IsPlaying() {
        return playing;
    }
    
    uint8_t GetVolume() {
        return masterVolume;
    }
    
    // PC Speaker beep implementation
    void Beep(uint32_t frequency, uint32_t duration_ms) {
        if (frequency == 0) return;
        
        // PIT channel 2 for speaker
        uint32_t div = 1193180 / frequency;
        
        // Set up PIT channel 2
        IO::outb(0x43, 0xB6);  // Channel 2, square wave
        IO::outb(0x42, div & 0xFF);
        IO::outb(0x42, (div >> 8) & 0xFF);
        
        // Enable speaker
        uint8_t tmp = IO::inb(0x61);
        IO::outb(0x61, tmp | 3);
        
        // Wait (simple delay loop)
        for (volatile uint32_t i = 0; i < duration_ms * 10000; i++);
        
        // Disable speaker
        IO::outb(0x61, tmp & 0xFC);
    }
    
    bool PlayFile(const char* path) {
        // WAV playback - to be implemented with mixer
        EarlyTerm::Print("[Audio] PlayFile: ");
        EarlyTerm::Print(path);
        EarlyTerm::Print(" (not yet implemented)\n");
        return false;
    }
}
