#pragma once

// MCL - Morphic Command Language Parser
// Natural Language Structured Command System

#include <stdint.h>

#define MCL_MAX_TOKENS 8
#define MCL_MAX_TOKEN_LEN 64
#define MCL_INPUT_BUFFER_SIZE 256

// Token types in MCL grammar
enum class MCLTokenType {
    NONE,
    ACTION,         // Verb: list, show, set, create, delete, etc.
    TARGET,         // Object: files, cpu, memory, layout, etc.
    MODIFIER,       // key:value pair
    VALUE,          // Raw value
    ERROR
};

// Parsed token
struct MCLToken {
    MCLTokenType type;
    char text[MCL_MAX_TOKEN_LEN];
    char key[32];           // For modifiers (key:value)
    char value[32];         // For modifiers (key:value)
};

// Command categories
enum class MCLCategory {
    UNKNOWN,
    STORAGE,        // list, read, create, delete files/folders
    HARDWARE,       // show, check, test, scan
    SYSTEM          // set, toggle, reboot, shutdown
};

// Parsed command structure
struct MCLCommand {
    MCLCategory category;
    MCLToken action;
    MCLToken target;
    MCLToken modifiers[4];
    int modifier_count;
    bool valid;
    const char* error_message;
};

// Action verb definitions
struct MCLVerb {
    const char* name;
    MCLCategory category;
    const char** valid_targets;
    int target_count;
};

// Thread-safe input buffer
struct MCLInputBuffer {
    char buffer[MCL_INPUT_BUFFER_SIZE];
    volatile int length;
    volatile bool ready;
    volatile bool processing;
};

namespace MCL {
    // Initialize the MCL parser
    void Init();
    
    // Parse a command string
    MCLCommand Parse(const char* input);
    
    // Execute a parsed command
    int Execute(const MCLCommand* cmd);
    
    // Get input buffer (thread-safe)
    MCLInputBuffer* GetInputBuffer();
    
    // Add character to input buffer (from keyboard ISR)
    void InputChar(char c);
    
    // Process ready input
    bool ProcessInput();
    
    // Get suggestions for current input
    const char** GetSuggestions(const char* partial, int* count);
    
    // Get last error message
    const char* GetLastError();
    
    // Register custom command handler
    typedef int (*MCLHandler)(const MCLCommand* cmd);
    void RegisterHandler(const char* action, MCLHandler handler);
}
