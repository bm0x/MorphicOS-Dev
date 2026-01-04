#include "mcl.h"
#include "morphic_syscalls.h"
#include "system_info.h"

// Minimal string utils for userspace
static int u_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static bool u_strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static void u_strcpy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void u_memset(void* ptr, int val, int size) {
    char* p = (char*)ptr;
    for (int i = 0; i < size; i++) p[i] = (char)val;
}

static void u_itoa(uint64_t val, char* buf) {
    if (val == 0) {
        buf[0] = '0'; buf[1] = 0;
        return;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        tmp[i++] = (val % 10) + '0';
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = 0;
}

namespace MCL {
    
    static const char* storage_targets[] = {"files", "folders", "mounts"};
    static const char* show_targets[] = {"cpu", "memory", "version", "disk"};
    static const char* set_targets[] = {"layout", "volume"};
    static const char* create_targets[] = {"file", "folder"};
    static const char* delete_targets[] = {"file", "folder"};
    static const char* scan_targets[] = {"bus"};
    
    struct MCLVerb {
        const char* name;
        MCLCategory category;
        const char** valid_targets;
        int target_count;
    };

    static MCLVerb verbs[] = {
        {"list",     MCLCategory::STORAGE,  storage_targets, 3},
        {"read",     MCLCategory::STORAGE,  nullptr, 0},
        {"create",   MCLCategory::STORAGE,  create_targets, 2},
        {"delete",   MCLCategory::STORAGE,  delete_targets, 2},
        {"open",     MCLCategory::STORAGE,  nullptr, 0},
        {"go",       MCLCategory::STORAGE,  nullptr, 0},
        {"show",     MCLCategory::STORAGE,  show_targets, 4},
        {"check",    MCLCategory::HARDWARE, nullptr, 0},
        {"test",     MCLCategory::HARDWARE, nullptr, 0},
        {"scan",     MCLCategory::HARDWARE, scan_targets, 1},
        {"set",      MCLCategory::SYSTEM,   set_targets, 2},
        {"toggle",   MCLCategory::SYSTEM,   nullptr, 0},
        {"reboot",   MCLCategory::SYSTEM,   nullptr, 0},
        {"shutdown", MCLCategory::SYSTEM,   nullptr, 0},
        {"help",     MCLCategory::SYSTEM,   nullptr, 0},
        {"clear",    MCLCategory::SYSTEM,   nullptr, 0},
    };

    static const int verbCount = sizeof(verbs) / sizeof(verbs[0]);

    void Init() {
        // Nothing to init in userspace port yet
    }

    // Simple parser (simplified from kernel version)
    static void ParseAndExecute(char* cmd, IOutput* out) {
        // Tokenize
        char* tokens[MCL_MAX_TOKENS];
        int tokenCount = 0;
        
        char* p = cmd;
        while (*p && tokenCount < MCL_MAX_TOKENS) {
            while (*p == ' ') p++; // Skip spaces
            if (!*p) break;
            
            tokens[tokenCount++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = 0; // Null terminate token
        }
        
        if (tokenCount == 0) return;

        // Match Verb
        MCLVerb* verb = nullptr;
        for (int i = 0; i < verbCount; i++) {
            if (u_strcmp(tokens[0], verbs[i].name)) {
                verb = &verbs[i];
                break;
            }
        }

        if (!verb) {
            out->Print("Unknown command: ");
            out->Print(tokens[0]);
            out->Print("\n");
            return;
        }

        // Execute
        if (u_strcmp(verb->name, "help")) {
            out->Print("Available commands:\n");
            out->Print("  help, clear, list files, show cpu, show memory, show disk\n");
            out->Print("  reboot, shutdown\n");
        }
        else if (u_strcmp(verb->name, "clear")) {
            // Handled by terminal usually, but we can print newlines
            out->Print("\n\n\n\n\n\n\n\n\n\n\n\n");
        }
        else if (u_strcmp(verb->name, "show")) {
            if (tokenCount < 2) {
                out->Print("Usage: show [cpu|memory|disk]\n");
                return;
            }
            
            MorphicSystemInfo si;
            if (sys_get_system_info(&si) != 1) {
                out->Print("Error: Failed to get system info.\n");
                return;
            }

            if (u_strcmp(tokens[1], "cpu")) {
                out->Print("CPU Vendor: ");
                out->Print(si.cpu_vendor);
                out->Print("\nBrand: ");
                out->Print(si.cpu_brand);
                out->Print("\n");
            }
            else if (u_strcmp(tokens[1], "memory")) {
                char buf[32];
                out->Print("Total Memory: ");
                u_itoa(si.total_mem_bytes / (1024*1024), buf);
                out->Print(buf);
                out->Print(" MB\n");
                
                out->Print("Free Memory:  ");
                u_itoa(si.free_mem_bytes / (1024*1024), buf);
                out->Print(buf);
                out->Print(" MB\n");
            }
            else if (u_strcmp(tokens[1], "disk")) {
                char buf[32];
                out->Print("Disk Total: ");
                u_itoa(si.disk_total_bytes / (1024*1024), buf);
                out->Print(buf);
                out->Print(" MB\n");
            }
            else {
                out->Print("Unknown target. Try: cpu, memory, disk\n");
            }
        }
        else if (u_strcmp(verb->name, "list")) {
             if (tokenCount >= 2 && u_strcmp(tokens[1], "files")) {
                 out->Print("Listing files (root):\n");
                 // TODO: Implement sys_read_dir or similar
                 out->Print("  [DIR]  EFI\n");
                 out->Print("  [FILE] morph_kernel.elf\n");
                 out->Print("  [FILE] desktop.mpk\n");
                 out->Print("  (Real VFS listing not yet implemented in userspace)\n");
             } else {
                 out->Print("Usage: list files\n");
             }
        }
        else {
            out->Print("Command recognized but not implemented yet.\n");
        }
    }

    void ProcessCommand(const char* cmdLine, IOutput* output) {
        if (!cmdLine || !output) return;
        
        char buf[MCL_INPUT_BUFFER_SIZE];
        u_strcpy(buf, cmdLine);
        
        ParseAndExecute(buf, output);
    }
}
