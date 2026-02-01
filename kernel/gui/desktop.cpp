// Morphic Desktop Launcher Implementation
// Desktop shell with taskbar, clock, and app menu

#include "desktop.h"
#include "widgets.h"
#include "../hal/video/graphics.h"
#include "../hal/video/early_term.h"
#include "../hal/input/mouse.h"
#include "../drivers/keyboard.h"
#include "../fs/vfs.h"
#include "../utils/std.h"
#include "../process/scheduler.h"

// External: PIT timer for frame timing
namespace PIT {
    extern uint64_t GetTicks();
}

namespace Desktop {

    // Desktop state
    static bool running = false;
    static bool menuVisible = false;
    
    // Widgets
    static Widget* btnApps = nullptr;
    static Widget* lblClock = nullptr;
    static Widget* pnlMenu = nullptr;
    static Widget* menuItems[8] = {nullptr};
    static int menuItemCount = 0;
    
    // Screen dimensions
    static uint32_t screenW = 1024;
    static uint32_t screenH = 768;
    static uint32_t taskbarH = 32;
    
    // Forward declarations
    static void OnAppsClick(Widget* w);
    static void OnMenuItemClick(Widget* w);
    
    void Init() {
        // Get screen dimensions from graphics
        screenW = Graphics::GetWidth();
        screenH = Graphics::GetHeight();
        
        EarlyTerm::Print("[Desktop] Initializing... ");
        EarlyTerm::PrintDec(screenW);
        EarlyTerm::Print("x");
        EarlyTerm::PrintDec(screenH);
        EarlyTerm::Print("\n");
        
        // Initialize widget system
        MorphicGUI::Init(screenW, screenH);
        
        // Create Apps button in taskbar
        btnApps = MorphicGUI::CreateButton("Apps", 4, screenH - taskbarH + 4, 60, 24, OnAppsClick);
        
        // Create clock label
        lblClock = MorphicGUI::CreateLabel("00:00", screenW - 50, screenH - taskbarH + 8, COLOR_WHITE);
        
        // Create app menu panel (hidden initially)
        pnlMenu = MorphicGUI::CreatePanel(4, screenH - taskbarH - 160, 150, 156, 0xFF202040);
        MorphicGUI::SetVisible(pnlMenu, false);
        
        // Create menu items
        const char* apps[] = {"Shell MCL", "System Info", "File Browser", "Settings"};
        for (int i = 0; i < 4; i++) {
            menuItems[i] = MorphicGUI::CreateButton(apps[i], 8, screenH - taskbarH - 156 + i * 36, 142, 32, OnMenuItemClick);
            menuItems[i]->userdata = (void*)(uint64_t)i;
            MorphicGUI::SetVisible(menuItems[i], false);
        }
        menuItemCount = 4;
        
        // === POST-COMPOSITION CURSOR SYSTEM ===
        // InitOverlay for cursor sprite access
        Mouse::InitOverlay(Graphics::GetDrawBuffer(), Graphics::GetPitch());
        Mouse::SetVisualContext(VisualContext::GRAPHICAL_GUI);
        
        // IMPORTANT: Disable IRQ12 fast path - we use post-composition cursor now
        // This prevents double-draw conflict (IRQ draws + post-flip draws)
        Mouse::EnableFastPath(false);
        
        EarlyTerm::Print("[Desktop] Post-composition cursor enabled.\n");
        
        running = true;
        menuVisible = false;
        
        EarlyTerm::Print("[Desktop] Ready.\n");

    }


    
    static void UpdateClock() {
        uint64_t uptime = MorphicGUI::GetUptime();
        uint32_t mins = (uptime / 60) % 60;
        uint32_t secs = uptime % 60;
        
        char clockText[16];
        clockText[0] = '0' + (mins / 10);
        clockText[1] = '0' + (mins % 10);
        clockText[2] = ':';
        clockText[3] = '0' + (secs / 10);
        clockText[4] = '0' + (secs % 10);
        clockText[5] = 0;
        
        MorphicGUI::SetText(lblClock, clockText);
    }
    
    static void OnAppsClick(Widget* w) {
        (void)w;
        if (menuVisible) {
            HideAppMenu();
        } else {
            ShowAppMenu();
        }
    }
    
    static void OnMenuItemClick(Widget* w) {
        int idx = (int)(uint64_t)w->userdata;
        HideAppMenu();
        
        switch (idx) {
            case 0:
                EarlyTerm::Print("\n[Desktop] Launching Shell MCL...\n");
                Stop();  // Exit to shell
                break;
            case 1:
                EarlyTerm::Print("\n[Desktop] System Info\n");
                EarlyTerm::Print("  Screen: ");
                EarlyTerm::PrintDec(screenW);
                EarlyTerm::Print("x");
                EarlyTerm::PrintDec(screenH);
                EarlyTerm::Print("\n  Uptime: ");
                EarlyTerm::PrintDec(MorphicGUI::GetUptime());
                EarlyTerm::Print(" seconds\n");
                break;
            case 2:
                EarlyTerm::Print("\n[Desktop] File Browser - coming soon\n");
                break;
            case 3:
                EarlyTerm::Print("\n[Desktop] Settings - coming soon\n");
                break;
        }
    }
    
    void ShowAppMenu() {
        menuVisible = true;
        MorphicGUI::SetVisible(pnlMenu, true);
        for (int i = 0; i < menuItemCount; i++) {
            MorphicGUI::SetVisible(menuItems[i], true);
        }
        MorphicGUI::MarkDirty();
    }
    
    void HideAppMenu() {
        menuVisible = false;
        MorphicGUI::SetVisible(pnlMenu, false);
        for (int i = 0; i < menuItemCount; i++) {
            MorphicGUI::SetVisible(menuItems[i], false);
        }
        MorphicGUI::MarkDirty();
    }
    
    void Run() {
        Init();
        
        EarlyTerm::Print("[Desktop] Entering main loop...\n");
        
        // Frame timing using PIT ticks
        const uint64_t FRAME_TICKS = 17;  // ~60Hz at 1000Hz PIT
        uint64_t lastTick = PIT::GetTicks();
        
        while (running) {

            // Update clock
            UpdateClock();

            
            // Process input events
            MorphicGUI::ProcessEvents();
            
            // Check for Escape to exit
            char c = Keyboard::GetChar();
            if (c == 27) {  // ESC
                EarlyTerm::Print("\n[Desktop] Exiting...\n");
                
                // === CLEAN CONTEXT SWITCH ===
                Mouse::SetVisualContext(VisualContext::TEXT_SHELL);
                
                // Full screen clear for clean text mode
                Graphics::FillRect(0, 0, Graphics::GetWidth(), Graphics::GetHeight(), 0xFF000000);  // Black
                Graphics::Flip();
                
                break;
            }
            
            // Render (Draw() calls FlipWithVSync() internally)
            MorphicGUI::Draw();

        }
    }


    
    void Stop() {
        running = false;
    }
    
    void LaunchApp(const char* path) {
        EarlyTerm::Print("[Desktop] Launch: ");
        EarlyTerm::Print(path);
        EarlyTerm::Print("\n");
        // TODO: Load and execute .mapp
    }
}
