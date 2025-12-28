#include "keymap.h"
#include "../video/early_term.h"
#include "../../utils/std.h"

namespace KeymapHAL {
    // Built-in keymaps
    static Keymap keymapUS;
    static Keymap keymapES;
    static Keymap keymapLA;
    
    static const Keymap* keymaps[8];
    static uint32_t keymapCount = 0;
    static const Keymap* currentKeymap = nullptr;
    
    // Modifier state
    static bool shiftPressed = false;
    static bool altgrPressed = false;
    static bool capsLock = false;
    
    void InitKeymapUS() {
        kmemset(&keymapUS, 0, sizeof(Keymap));
        keymapUS.id[0] = 'U'; keymapUS.id[1] = 'S'; keymapUS.id[2] = 0;
        
        // Normal keys (no modifiers)
        keymapUS.normal[0x02] = '1'; keymapUS.normal[0x03] = '2';
        keymapUS.normal[0x04] = '3'; keymapUS.normal[0x05] = '4';
        keymapUS.normal[0x06] = '5'; keymapUS.normal[0x07] = '6';
        keymapUS.normal[0x08] = '7'; keymapUS.normal[0x09] = '8';
        keymapUS.normal[0x0A] = '9'; keymapUS.normal[0x0B] = '0';
        keymapUS.normal[0x0C] = '-'; keymapUS.normal[0x0D] = '=';
        keymapUS.normal[0x0E] = '\b'; keymapUS.normal[0x0F] = '\t';
        
        keymapUS.normal[0x10] = 'q'; keymapUS.normal[0x11] = 'w';
        keymapUS.normal[0x12] = 'e'; keymapUS.normal[0x13] = 'r';
        keymapUS.normal[0x14] = 't'; keymapUS.normal[0x15] = 'y';
        keymapUS.normal[0x16] = 'u'; keymapUS.normal[0x17] = 'i';
        keymapUS.normal[0x18] = 'o'; keymapUS.normal[0x19] = 'p';
        keymapUS.normal[0x1A] = '['; keymapUS.normal[0x1B] = ']';
        keymapUS.normal[0x1C] = '\n';
        
        keymapUS.normal[0x1E] = 'a'; keymapUS.normal[0x1F] = 's';
        keymapUS.normal[0x20] = 'd'; keymapUS.normal[0x21] = 'f';
        keymapUS.normal[0x22] = 'g'; keymapUS.normal[0x23] = 'h';
        keymapUS.normal[0x24] = 'j'; keymapUS.normal[0x25] = 'k';
        keymapUS.normal[0x26] = 'l'; keymapUS.normal[0x27] = ';';
        keymapUS.normal[0x28] = '\''; keymapUS.normal[0x29] = '`';
        keymapUS.normal[0x2B] = '\\';
        
        keymapUS.normal[0x2C] = 'z'; keymapUS.normal[0x2D] = 'x';
        keymapUS.normal[0x2E] = 'c'; keymapUS.normal[0x2F] = 'v';
        keymapUS.normal[0x30] = 'b'; keymapUS.normal[0x31] = 'n';
        keymapUS.normal[0x32] = 'm'; keymapUS.normal[0x33] = ',';
        keymapUS.normal[0x34] = '.'; keymapUS.normal[0x35] = '/';
        keymapUS.normal[0x39] = ' ';
        
        // Shift keys
        keymapUS.shift[0x02] = '!'; keymapUS.shift[0x03] = '@';
        keymapUS.shift[0x04] = '#'; keymapUS.shift[0x05] = '$';
        keymapUS.shift[0x06] = '%'; keymapUS.shift[0x07] = '^';
        keymapUS.shift[0x08] = '&'; keymapUS.shift[0x09] = '*';
        keymapUS.shift[0x0A] = '('; keymapUS.shift[0x0B] = ')';
        keymapUS.shift[0x0C] = '_'; keymapUS.shift[0x0D] = '+';
        
        // Uppercase letters
        for (int i = 0x10; i <= 0x19; i++) keymapUS.shift[i] = keymapUS.normal[i] - 32;
        for (int i = 0x1E; i <= 0x26; i++) keymapUS.shift[i] = keymapUS.normal[i] - 32;
        for (int i = 0x2C; i <= 0x32; i++) keymapUS.shift[i] = keymapUS.normal[i] - 32;
        
        keymapUS.shift[0x1A] = '{'; keymapUS.shift[0x1B] = '}';
        keymapUS.shift[0x27] = ':'; keymapUS.shift[0x28] = '"';
        keymapUS.shift[0x29] = '~'; keymapUS.shift[0x2B] = '|';
        keymapUS.shift[0x33] = '<'; keymapUS.shift[0x34] = '>';
        keymapUS.shift[0x35] = '?';
    }
    
    void InitKeymapES() {
        // Spanish ISO keyboard layout - different from US ANSI
        kmemset(&keymapES, 0, sizeof(Keymap));
        keymapES.id[0] = 'E'; keymapES.id[1] = 'S'; keymapES.id[2] = 0;
        
        // Number row - Spanish layout
        keymapES.normal[0x02] = '1'; keymapES.shift[0x02] = '!';
        keymapES.normal[0x03] = '2'; keymapES.shift[0x03] = '"'; keymapES.altgr[0x03] = '@';
        keymapES.normal[0x04] = '3'; keymapES.shift[0x04] = '#'; keymapES.altgr[0x04] = '#';
        keymapES.normal[0x05] = '4'; keymapES.shift[0x05] = '$';
        keymapES.normal[0x06] = '5'; keymapES.shift[0x06] = '%';
        keymapES.normal[0x07] = '6'; keymapES.shift[0x07] = '&';
        keymapES.normal[0x08] = '7'; keymapES.shift[0x08] = '/';
        keymapES.normal[0x09] = '8'; keymapES.shift[0x09] = '(';
        keymapES.normal[0x0A] = '9'; keymapES.shift[0x0A] = ')';
        keymapES.normal[0x0B] = '0'; keymapES.shift[0x0B] = '=';
        keymapES.normal[0x0C] = '\''; keymapES.shift[0x0C] = '?';
        keymapES.normal[0x0D] = '!'; keymapES.shift[0x0D] = '?'; // inverted
        keymapES.normal[0x0E] = '\b';
        keymapES.normal[0x0F] = '\t';
        
        // Top letter row QWERTY
        keymapES.normal[0x10] = 'q'; keymapES.shift[0x10] = 'Q';
        keymapES.normal[0x11] = 'w'; keymapES.shift[0x11] = 'W';
        keymapES.normal[0x12] = 'e'; keymapES.shift[0x12] = 'E';
        keymapES.normal[0x13] = 'r'; keymapES.shift[0x13] = 'R';
        keymapES.normal[0x14] = 't'; keymapES.shift[0x14] = 'T';
        keymapES.normal[0x15] = 'y'; keymapES.shift[0x15] = 'Y';
        keymapES.normal[0x16] = 'u'; keymapES.shift[0x16] = 'U';
        keymapES.normal[0x17] = 'i'; keymapES.shift[0x17] = 'I';
        keymapES.normal[0x18] = 'o'; keymapES.shift[0x18] = 'O';
        keymapES.normal[0x19] = 'p'; keymapES.shift[0x19] = 'P';
        keymapES.normal[0x1A] = '`'; keymapES.shift[0x1A] = '^';
        keymapES.normal[0x1B] = '+'; keymapES.shift[0x1B] = '*';
        keymapES.normal[0x1C] = '\n';
        
        // Middle letter row ASDF
        keymapES.normal[0x1E] = 'a'; keymapES.shift[0x1E] = 'A';
        keymapES.normal[0x1F] = 's'; keymapES.shift[0x1F] = 'S';
        keymapES.normal[0x20] = 'd'; keymapES.shift[0x20] = 'D';
        keymapES.normal[0x21] = 'f'; keymapES.shift[0x21] = 'F';
        keymapES.normal[0x22] = 'g'; keymapES.shift[0x22] = 'G';
        keymapES.normal[0x23] = 'h'; keymapES.shift[0x23] = 'H';
        keymapES.normal[0x24] = 'j'; keymapES.shift[0x24] = 'J';
        keymapES.normal[0x25] = 'k'; keymapES.shift[0x25] = 'K';
        keymapES.normal[0x26] = 'l'; keymapES.shift[0x26] = 'L';
        keymapES.normal[0x27] = 0xF1; keymapES.shift[0x27] = 0xD1;  // ñ Ñ
        keymapES.normal[0x28] = '\''; keymapES.shift[0x28] = '"';
        keymapES.normal[0x29] = '|'; keymapES.shift[0x29] = '\\';
        keymapES.normal[0x2B] = 0xE7; keymapES.shift[0x2B] = 0xC7;  // ç Ç
        
        // Bottom letter row ZXCV
        keymapES.normal[0x2C] = 'z'; keymapES.shift[0x2C] = 'Z';
        keymapES.normal[0x2D] = 'x'; keymapES.shift[0x2D] = 'X';
        keymapES.normal[0x2E] = 'c'; keymapES.shift[0x2E] = 'C';
        keymapES.normal[0x2F] = 'v'; keymapES.shift[0x2F] = 'V';
        keymapES.normal[0x30] = 'b'; keymapES.shift[0x30] = 'B';
        keymapES.normal[0x31] = 'n'; keymapES.shift[0x31] = 'N';
        keymapES.normal[0x32] = 'm'; keymapES.shift[0x32] = 'M';
        keymapES.normal[0x33] = ','; keymapES.shift[0x33] = ';';
        keymapES.normal[0x34] = '.'; keymapES.shift[0x34] = ':';
        keymapES.normal[0x35] = '-'; keymapES.shift[0x35] = '_';
        
        // Space
        keymapES.normal[0x39] = ' ';
        
        // Extra key on ISO keyboard (between left shift and Z)
        keymapES.normal[0x56] = '<'; keymapES.shift[0x56] = '>';
    }
    
    void InitKeymapLA() {
        // Copy ES keymap (which is already initialized)
        kmemcpy(&keymapLA, &keymapES, sizeof(Keymap));
        
        // Set LA identifier
        keymapLA.id[0] = 'L'; keymapLA.id[1] = 'A'; keymapLA.id[2] = 0;
        
        // Regenerate uppercase letters 
        for (int i = 0x10; i <= 0x19; i++) {
            if (keymapLA.normal[i] >= 'a' && keymapLA.normal[i] <= 'z')
                keymapLA.shift[i] = keymapLA.normal[i] - 32;
        }
        for (int i = 0x1E; i <= 0x26; i++) {
            if (keymapLA.normal[i] >= 'a' && keymapLA.normal[i] <= 'z')
                keymapLA.shift[i] = keymapLA.normal[i] - 32;
        }
        for (int i = 0x2C; i <= 0x32; i++) {
            if (keymapLA.normal[i] >= 'a' && keymapLA.normal[i] <= 'z')
                keymapLA.shift[i] = keymapLA.normal[i] - 32;
        }
    }

    void Init() {

        InitKeymapUS();
        InitKeymapES();
        InitKeymapLA();
        
        keymaps[0] = &keymapUS;
        keymaps[1] = &keymapES;
        keymaps[2] = &keymapLA;
        keymapCount = 3;
        
        currentKeymap = &keymapUS;  // Default
        
        EarlyTerm::Print("[Keymap] Initialized: US, ES, LA\n");
    }
    
    bool SetKeymap(const char* id) {
        for (uint32_t i = 0; i < keymapCount; i++) {
            if (kstrcmp(keymaps[i]->id, id) == 0) {
                currentKeymap = keymaps[i];
                EarlyTerm::Print("[Keymap] Switched to: ");
                EarlyTerm::Print(id);
                EarlyTerm::Print("\n");
                return true;
            }
        }
        return false;
    }
    
    const Keymap* GetCurrentKeymap() {
        return currentKeymap;
    }
    
    uint16_t Translate(uint8_t scancode, bool shift, bool altgr, bool caps) {
        if (!currentKeymap || scancode >= 128) return 0;
        
        if (altgr && currentKeymap->altgr[scancode]) {
            return currentKeymap->altgr[scancode];
        }
        if (shift || caps) {
            if (currentKeymap->shift[scancode]) {
                return currentKeymap->shift[scancode];
            }
        }
        return currentKeymap->normal[scancode];
    }
    
    const Keymap* GetKeymap(const char* id) {
        for (uint32_t i = 0; i < keymapCount; i++) {
            if (kstrcmp(keymaps[i]->id, id) == 0) {
                return keymaps[i];
            }
        }
        return nullptr;
    }
    
    void RegisterKeymap(const Keymap* keymap) {
        if (keymapCount < 8) {
            keymaps[keymapCount++] = keymap;
        }
    }
    
    bool IsModifier(uint8_t scancode) {
        return scancode == KEY_LSHIFT || scancode == KEY_RSHIFT ||
               scancode == KEY_LCTRL || scancode == KEY_LALT ||
               scancode == KEY_RALT || scancode == KEY_CAPS;
    }
    
    uint32_t GetKeymapCount() {
        return keymapCount;
    }
}
