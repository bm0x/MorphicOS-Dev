#include "bootconfig.h"
#include "../fs/vfs.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

namespace BootConfiguration {
    static BootConfig config;
    
    void Init() {
        // Set defaults
        config.show_logo = true;
        config.verbose_mode = true;
        config.enable_sound = false;
        config.timeout_ms = 0;
        config.log_level = 3;  // INFO level
        config.keymap[0] = 'U';
        config.keymap[1] = 'S';
        config.keymap[2] = 0;
    }
    
    // Simple key=value parser
    static void ParseLine(const char* line) {
        // Skip whitespace
        while (*line == ' ' || *line == '\t') line++;
        
        // Skip comments and empty lines
        if (*line == '#' || *line == '\n' || *line == 0) return;
        
        // Find '='
        const char* eq = line;
        while (*eq && *eq != '=') eq++;
        if (!*eq) return;
        
        // Get key length
        int keyLen = eq - line;
        
        // Get value
        const char* val = eq + 1;
        while (*val == ' ') val++;
        
        // Parse known keys
        if (keyLen == 8 && kmemcmp(line, "showlogo", 8) == 0) {
            config.show_logo = (*val == '1' || *val == 't' || *val == 'y');
        } 
        else if (keyLen == 7 && kmemcmp(line, "verbose", 7) == 0) {
            config.verbose_mode = (*val == '1' || *val == 't' || *val == 'y');
        }
        else if (keyLen == 5 && kmemcmp(line, "sound", 5) == 0) {
            config.enable_sound = (*val == '1' || *val == 't' || *val == 'y');
        }
        else if (keyLen == 7 && kmemcmp(line, "timeout", 7) == 0) {
            config.timeout_ms = 0;
            while (*val >= '0' && *val <= '9') {
                config.timeout_ms = config.timeout_ms * 10 + (*val - '0');
                val++;
            }
        }
        else if (keyLen == 8 && kmemcmp(line, "loglevel", 8) == 0) {
            config.log_level = (*val >= '0' && *val <= '4') ? (*val - '0') : 3;
        }
        else if (keyLen == 6 && kmemcmp(line, "keymap", 6) == 0) {
            // Copy keymap ID (max 7 chars)
            int i = 0;
            while (*val && *val != '\n' && *val != ' ' && i < 7) {
                config.keymap[i++] = *val++;
            }
            config.keymap[i] = 0;
        }
    }
    
    bool LoadFromFile(const char* path) {
        VFSNode* node = VFS::Open(path);
        if (!node) return false;
        
        uint8_t buffer[256];
        int32_t bytesRead = VFS::Read(node, 0, 255, buffer);
        VFS::Close(node);
        
        if (bytesRead <= 0) return false;
        
        buffer[bytesRead] = 0;
        
        // Parse line by line
        char line[64];
        int lineLen = 0;
        
        for (int i = 0; i <= bytesRead; i++) {
            if (buffer[i] == '\n' || buffer[i] == 0) {
                line[lineLen] = 0;
                ParseLine(line);
                lineLen = 0;
            } else if (lineLen < 63) {
                line[lineLen++] = buffer[i];
            }
        }
        
        EarlyTerm::Print("[BootConfig] Loaded from ");
        EarlyTerm::Print(path);
        EarlyTerm::Print("\n");
        
        return true;
    }
    
    BootConfig* GetConfig() { return &config; }
    bool ShowLogo() { return config.show_logo; }
    bool VerboseMode() { return config.verbose_mode; }
    bool SoundEnabled() { return config.enable_sound; }
    uint32_t GetTimeout() { return config.timeout_ms; }
    uint32_t GetLogLevel() { return config.log_level; }
    const char* GetKeymap() { return config.keymap; }
}

