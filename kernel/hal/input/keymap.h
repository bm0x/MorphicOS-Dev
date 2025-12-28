#pragma once

#include <stdint.h>

// Keymap structure for keyboard layout abstraction
struct Keymap {
    char id[8];             // "US", "ES", "LA"
    char name[32];          // "English (US)", "Español (ES)"
    
    // Scancode to Unicode mappings
    uint16_t normal[128];   // No modifiers
    uint16_t shift[128];    // With Shift
    uint16_t altgr[128];    // With AltGr (Right Alt)
    uint16_t caps[128];     // With Caps Lock
};

// Special key scancodes
#define KEY_F1      0x3B
#define KEY_F2      0x3C
#define KEY_F3      0x3D
#define KEY_F4      0x3E
#define KEY_F5      0x3F
#define KEY_F6      0x40
#define KEY_F7      0x41
#define KEY_F8      0x42
#define KEY_F9      0x43
#define KEY_F10     0x44
#define KEY_F11     0x57
#define KEY_F12     0x58
#define KEY_LSHIFT  0x2A
#define KEY_RSHIFT  0x36
#define KEY_LCTRL   0x1D
#define KEY_LALT    0x38
#define KEY_RALT    0x38  // Extended: E0 38
#define KEY_CAPS    0x3A

// Keymap HAL - keyboard layout management
namespace KeymapHAL {
    // Initialize with default layout
    void Init();
    
    // Set active keymap by ID ("US", "ES", "LA")
    bool SetKeymap(const char* id);
    
    // Get current keymap
    const Keymap* GetCurrentKeymap();
    
    // Translate scancode to character
    // Returns Unicode codepoint (or ASCII for basic chars)
    uint16_t Translate(uint8_t scancode, bool shift, bool altgr, bool caps);
    
    // Get keymap by ID
    const Keymap* GetKeymap(const char* id);
    
    // Register a new keymap
    void RegisterKeymap(const Keymap* keymap);
    
    // Check if modifier key
    bool IsModifier(uint8_t scancode);
    
    // Get available keymap count
    uint32_t GetKeymapCount();
}
