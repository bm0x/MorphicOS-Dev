#pragma once

// Morphic Desktop Launcher
// Main desktop shell with taskbar and app menu

#include "widgets.h"

namespace Desktop {
    // Initialize desktop environment
    void Init();
    
    // Main event loop
    void Run();
    
    // Stop desktop
    void Stop();
    
    // Menu control
    void ShowAppMenu();
    void HideAppMenu();
    
    // Launch application
    void LaunchApp(const char* path);
}
