#pragma once
#include <stdint.h>

namespace BootScreen {
    // Initialize the boot screen (clears display, draws logo)
    void Init();
    
    // Update progress bar and status message
    // percent: 0 to 100
    void Update(uint32_t percent, const char* message);
    
    // Clean up and prepare for shell/desktop
    void Finish();
    
    // Quick method to just set message without changing progress
    void Log(const char* message);
}
