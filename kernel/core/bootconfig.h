#pragma once

#include <stdint.h>

// Boot configuration structure
struct BootConfig {
    bool show_logo;        // Display splash screen
    bool verbose_mode;     // Show detailed boot messages
    bool enable_sound;     // Enable system sounds
    uint32_t timeout_ms;   // Boot timeout in milliseconds
    uint32_t log_level;    // 0=none, 1=err, 2=warn, 3=info, 4=debug
    char keymap[8];        // Keyboard layout ID (US, ES, LA)
};

namespace BootConfiguration {
    // Initialize with defaults
    void Init();
    
    // Load from VFS file (/etc/boot.cfg)
    bool LoadFromFile(const char* path);
    
    // Get current config
    BootConfig* GetConfig();
    
    // Individual settings
    bool ShowLogo();
    bool VerboseMode();
    bool SoundEnabled();
    uint32_t GetTimeout();
    uint32_t GetLogLevel();
    const char* GetKeymap();
}

