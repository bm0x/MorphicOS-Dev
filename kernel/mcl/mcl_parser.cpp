// MCL Parser Implementation
// Tokenizer and command dispatcher

#include "mcl_parser.h"
#include "../arch/common/spinlock.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"

namespace MCL {
    // Input buffer with lock
    static MCLInputBuffer inputBuffer;
    static Spinlock inputLock = SPINLOCK_INIT;
    
    // Last error message
    static const char* lastError = nullptr;
    
    // Verb definitions with valid targets
    static const char* storage_targets[] = {"files", "folders", "mounts"};
    static const char* show_targets[] = {"cpu", "memory", "version"};
    static const char* set_targets[] = {"layout", "volume"};
    static const char* create_targets[] = {"file", "folder"};
    static const char* delete_targets[] = {"file", "folder"};
    static const char* scan_targets[] = {"bus"};
    
    static MCLVerb verbs[] = {
        {"list",     MCLCategory::STORAGE,  storage_targets, 3},
        {"read",     MCLCategory::STORAGE,  nullptr, 0},
        {"create",   MCLCategory::STORAGE,  create_targets, 2},
        {"delete",   MCLCategory::STORAGE,  delete_targets, 2},
        {"open",     MCLCategory::STORAGE,  nullptr, 0},  // open folder:name
        {"go",       MCLCategory::STORAGE,  nullptr, 0},  // go back
        {"show",     MCLCategory::STORAGE,  show_targets, 3},  // show path, cpu, memory
        {"check",    MCLCategory::HARDWARE, nullptr, 0},
        {"test",     MCLCategory::HARDWARE, nullptr, 0},
        {"scan",     MCLCategory::HARDWARE, scan_targets, 1},
        {"set",      MCLCategory::SYSTEM,   set_targets, 2},
        {"toggle",   MCLCategory::SYSTEM,   nullptr, 0},
        {"reboot",   MCLCategory::SYSTEM,   nullptr, 0},
        {"shutdown", MCLCategory::SYSTEM,   nullptr, 0},
    };

    static const int verbCount = sizeof(verbs) / sizeof(verbs[0]);
    
    void Init() {
        kmemset(&inputBuffer, 0, sizeof(inputBuffer));
        inputBuffer.ready = false;
        inputBuffer.processing = false;
        lastError = nullptr;
    }
    
    MCLInputBuffer* GetInputBuffer() {
        return &inputBuffer;
    }
    
    void InputChar(char c) {
        CRITICAL_SECTION(inputLock);
        
        if (inputBuffer.processing) return;
        
        if (c == '\n') {
            inputBuffer.ready = true;
        } else if (c == '\b') {
            if (inputBuffer.length > 0) {
                inputBuffer.length--;
                inputBuffer.buffer[inputBuffer.length] = 0;
            }
        } else if (inputBuffer.length < MCL_INPUT_BUFFER_SIZE - 1) {
            inputBuffer.buffer[inputBuffer.length++] = c;
            inputBuffer.buffer[inputBuffer.length] = 0;
        }
    }
    
    bool ProcessInput() {
        if (!inputBuffer.ready) return false;
        
        {
            CRITICAL_SECTION(inputLock);
            inputBuffer.processing = true;
        }
        
        // Parse and execute
        MCLCommand cmd = Parse(inputBuffer.buffer);
        if (cmd.valid) {
            Execute(&cmd);
        } else {
            EarlyTerm::Print("[UNKNOWN] ");
            EarlyTerm::Print(cmd.error_message ? cmd.error_message : "Invalid command");
            EarlyTerm::Print("\n");
        }
        
        {
            CRITICAL_SECTION(inputLock);
            inputBuffer.length = 0;
            inputBuffer.buffer[0] = 0;
            inputBuffer.ready = false;
            inputBuffer.processing = false;
        }
        
        return true;
    }
    
    // Tokenize input: "action target modifier:value"
    static int Tokenize(const char* input, MCLToken* tokens, int maxTokens) {
        int tokenCount = 0;
        const char* p = input;
        
        while (*p && tokenCount < maxTokens) {
            // Skip whitespace
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            
            MCLToken* tok = &tokens[tokenCount];
            kmemset(tok, 0, sizeof(MCLToken));
            
            int i = 0;
            bool hasColon = false;
            int colonPos = -1;
            
            // Read until whitespace
            while (*p && *p != ' ' && *p != '\t' && i < MCL_MAX_TOKEN_LEN - 1) {
                if (*p == ':' && !hasColon) {
                    hasColon = true;
                    colonPos = i;
                }
                tok->text[i++] = *p++;
            }
            tok->text[i] = 0;
            
            // Determine token type
            if (tokenCount == 0) {
                tok->type = MCLTokenType::ACTION;
            } else if (hasColon) {
                tok->type = MCLTokenType::MODIFIER;
                // Split key:value
                for (int j = 0; j < colonPos && j < 31; j++) {
                    tok->key[j] = tok->text[j];
                }
                tok->key[colonPos] = 0;
                int vi = 0;
                for (int j = colonPos + 1; tok->text[j] && vi < 31; j++, vi++) {
                    tok->value[vi] = tok->text[j];
                }
                tok->value[vi] = 0;
            } else {
                tok->type = MCLTokenType::TARGET;
            }
            
            tokenCount++;
        }
        
        return tokenCount;
    }
    
    MCLCommand Parse(const char* input) {
        MCLCommand cmd;
        kmemset(&cmd, 0, sizeof(cmd));
        cmd.valid = false;
        
        if (!input || !*input) {
            cmd.error_message = "Empty command";
            return cmd;
        }
        
        MCLToken tokens[MCL_MAX_TOKENS];
        int tokenCount = Tokenize(input, tokens, MCL_MAX_TOKENS);
        
        if (tokenCount == 0) {
            cmd.error_message = "No tokens found";
            return cmd;
        }
        
        // First token must be an action
        kmemcpy(&cmd.action, &tokens[0], sizeof(MCLToken));
        
        // Find matching verb
        MCLVerb* matchedVerb = nullptr;
        for (int i = 0; i < verbCount; i++) {
            if (kstrcmp(verbs[i].name, cmd.action.text) == 0) {
                matchedVerb = &verbs[i];
                cmd.category = matchedVerb->category;
                break;
            }
        }
        
        if (!matchedVerb) {
            cmd.error_message = "Unknown action";
            lastError = cmd.error_message;
            return cmd;
        }
        
        // Process remaining tokens
        for (int i = 1; i < tokenCount; i++) {
            if (tokens[i].type == MCLTokenType::TARGET && cmd.target.type == MCLTokenType::NONE) {
                kmemcpy(&cmd.target, &tokens[i], sizeof(MCLToken));
            } else if (tokens[i].type == MCLTokenType::MODIFIER) {
                if (cmd.modifier_count < 4) {
                    kmemcpy(&cmd.modifiers[cmd.modifier_count++], &tokens[i], sizeof(MCLToken));
                }
            } else if (tokens[i].type == MCLTokenType::TARGET && cmd.target.type != MCLTokenType::NONE) {
                // Extra TARGET tokens become modifiers (for "create file name" syntax)
                if (cmd.modifier_count < 4) {
                    MCLToken* mod = &cmd.modifiers[cmd.modifier_count++];
                    mod->type = MCLTokenType::MODIFIER;
                    // Copy text as both key and full text
                    kmemcpy(mod->key, tokens[i].text, 32);
                    mod->key[31] = 0;
                    mod->value[0] = 0;  // No value
                    kmemcpy(mod->text, tokens[i].text, MCL_MAX_TOKEN_LEN);
                }
            }
        }

        
        cmd.valid = true;
        return cmd;
    }
    
    const char* GetLastError() {
        return lastError;
    }
    
    // Forward declarations for command handlers
    int ExecuteStorage(const MCLCommand* cmd);
    int ExecuteHardware(const MCLCommand* cmd);
    int ExecuteSystem(const MCLCommand* cmd);
    
    int Execute(const MCLCommand* cmd) {
        if (!cmd || !cmd->valid) return -1;
        
        switch (cmd->category) {
            case MCLCategory::STORAGE:
                return ExecuteStorage(cmd);
            case MCLCategory::HARDWARE:
                return ExecuteHardware(cmd);
            case MCLCategory::SYSTEM:
                return ExecuteSystem(cmd);
            default:
                EarlyTerm::Print("[ERROR] Unknown command category\n");
                return -1;
        }
    }
    
    const char** GetSuggestions(const char* partial, int* count) {
        static const char* suggestions[8];
        *count = 0;
        
        if (!partial || !*partial) {
            // Suggest all verbs
            for (int i = 0; i < verbCount && *count < 8; i++) {
                suggestions[(*count)++] = verbs[i].name;
            }
            return suggestions;
        }
        
        // Find matching verb
        for (int i = 0; i < verbCount; i++) {
            if (kstrcmp(verbs[i].name, partial) == 0) {
                // Return valid targets for this verb
                for (int j = 0; j < verbs[i].target_count && *count < 8; j++) {
                    suggestions[(*count)++] = verbs[i].valid_targets[j];
                }
                return suggestions;
            }
        }
        
        // Partial match on verbs
        int partialLen = kstrlen(partial);
        for (int i = 0; i < verbCount && *count < 8; i++) {
            bool match = true;
            for (int j = 0; j < partialLen; j++) {
                if (verbs[i].name[j] != partial[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                suggestions[(*count)++] = verbs[i].name;
            }
        }
        
        return suggestions;
    }
}
