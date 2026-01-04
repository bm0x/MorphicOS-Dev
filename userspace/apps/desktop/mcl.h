#pragma once

#include <stdint.h>

// MCL - Morphic Command Language (Userspace Port)

#define MCL_MAX_TOKENS 8
#define MCL_MAX_TOKEN_LEN 64
#define MCL_INPUT_BUFFER_SIZE 256

enum class MCLTokenType {
    NONE,
    ACTION,
    TARGET,
    MODIFIER,
    VALUE,
    ERROR
};

struct MCLToken {
    MCLTokenType type;
    char text[MCL_MAX_TOKEN_LEN];
    char key[32];
    char value[32];
};

enum class MCLCategory {
    UNKNOWN,
    STORAGE,
    HARDWARE,
    SYSTEM
};

struct MCLCommand {
    MCLCategory category;
    MCLToken action;
    MCLToken target;
    MCLToken modifiers[4];
    int modifier_count;
    bool valid;
    const char* error_message;
};

struct MCLInputBuffer {
    char buffer[MCL_INPUT_BUFFER_SIZE];
    int length;
    bool ready;
    bool processing;
};

// Callback interface for printing output
struct IOutput {
    virtual void Print(const char* text) = 0;
};

namespace MCL {
    void Init();
    void ProcessCommand(const char* cmdLine, IOutput* output);
}
