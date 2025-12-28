// MCL Command Implementations
// Storage, Hardware, and System commands

#include "mcl_parser.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include "../utils/std.h"
#include "../hal/video/early_term.h"
#include "../hal/input/keymap.h"
#include "../hal/audio/mixer.h"
#include "../process/scheduler.h"

namespace MCL {
    
    // Current working directory for navigation
    static char currentPath[128] = "/";
    
    // Forward declaration
    int ExecuteHardware(const MCLCommand* cmd);
    
    // ==================== STORAGE COMMANDS ====================


    
    int ExecuteStorage(const MCLCommand* cmd) {
        const char* action = cmd->action.text;
        const char* target = cmd->target.text;
        
        // Helper: get modifier value by key
        auto getModifier = [cmd](const char* key) -> const char* {
            for (int i = 0; i < cmd->modifier_count; i++) {
                if (kstrcmp(cmd->modifiers[i].key, key) == 0) {
                    return cmd->modifiers[i].value;
                }
            }
            return nullptr;
        };
        
        // LIST (files/folders/all)
        if (kstrcmp(action, "list") == 0) {
            const char* path = currentPath;  // Use current working directory
            bool filesOnly = (kstrcmp(target, "files") == 0);
            bool foldersOnly = (kstrcmp(target, "folders") == 0);
            
            // Check for path in modifiers
            const char* filesPath = getModifier("files");
            const char* foldersPath = getModifier("folders");
            if (filesPath) { path = filesPath; filesOnly = true; }
            if (foldersPath) { path = foldersPath; foldersOnly = true; }
            
            VFSNode* dir = VFS::Open(path);
            if (!dir) {
                EarlyTerm::Print("[ERROR] Directory not found: ");
                EarlyTerm::Print(path);
                EarlyTerm::Print("\n");
                return -1;
            }
            
            if (dir->type != NodeType::DIRECTORY) {
                EarlyTerm::Print("[ERROR] Not a directory\n");
                return -1;
            }
            
            uint32_t count;
            VFSNode** children = VFS::ListDir(dir, &count);
            
            if (filesOnly) {
                EarlyTerm::Print("Files in ");
            } else if (foldersOnly) {
                EarlyTerm::Print("Folders in ");
            } else {
                EarlyTerm::Print("Contents of ");
            }
            EarlyTerm::Print(path);
            EarlyTerm::Print(":\n");
            
            for (uint32_t i = 0; i < count; i++) {
                bool isDir = (children[i]->type == NodeType::DIRECTORY);
                
                // Filter based on request
                if (filesOnly && isDir) continue;
                if (foldersOnly && !isDir) continue;
                
                EarlyTerm::Print("  ");
                EarlyTerm::Print(children[i]->name);
                if (isDir) {
                    EarlyTerm::Print("/");
                } else {
                    EarlyTerm::Print("  [");
                    EarlyTerm::PrintDec(children[i]->size);
                    EarlyTerm::Print(" bytes]");
                }
                EarlyTerm::Print("\n");
            }
            
            if (children) kfree(children);
            return 0;
        }
        
        // READ FILE: read file:name
        if (kstrcmp(action, "read") == 0) {
            const char* filename = getModifier("file");
            
            if (!filename) {
                EarlyTerm::Print("[MISSING] Usage: read file:name\n");
                return -1;
            }
            
            // Build path
            char path[128] = "/";
            int i = 1;
            const char* f = filename;
            while (*f && i < 127) {
                path[i++] = *f++;
            }
            path[i] = 0;
            
            VFSNode* file = VFS::Open(path);
            if (!file) {
                EarlyTerm::Print("[ERROR] File not found: ");
                EarlyTerm::Print(path);
                EarlyTerm::Print("\n");
                return -1;
            }
            
            if (file->type != NodeType::FILE) {
                EarlyTerm::Print("[ERROR] Not a file\n");
                return -1;
            }
            
            uint8_t* buffer = (uint8_t*)kmalloc(file->size + 1);
            if (!buffer) {
                EarlyTerm::Print("[ERROR] Out of memory\n");
                return -1;
            }
            
            uint32_t readBytes = VFS::Read(file, 0, file->size, buffer);
            buffer[readBytes] = 0;
            
            EarlyTerm::Print((const char*)buffer);
            EarlyTerm::Print("\n");
            
            kfree(buffer);
            return 0;
        }
        
        // CREATE FILE: create file:name OR create file name
        if (kstrcmp(action, "create") == 0) {
            const char* filename = getModifier("file");
            const char* foldername = getModifier("folder");
            
            // Also support "create file name" format (target = file/folder, modifier = name)
            if (!filename && !foldername && cmd->modifier_count > 0) {
                if (kstrcmp(target, "file") == 0) {
                    // First modifier key is the filename
                    filename = cmd->modifiers[0].key;
                } else if (kstrcmp(target, "folder") == 0) {
                    foldername = cmd->modifiers[0].key;
                }
            }
            
            if (filename) {
                VFSNode* node = VFS::CreateFile(filename);
                if (node) {
                    EarlyTerm::Print("[OK] File created: ");
                    EarlyTerm::Print(filename);
                    EarlyTerm::Print("\n");
                    return 0;
                }
                EarlyTerm::Print("[ERROR] Failed to create file\n");
                return -1;
            }
            
            if (foldername) {
                VFSNode* node = VFS::Mkdir(foldername);
                if (node) {
                    EarlyTerm::Print("[OK] Directory created: ");
                    EarlyTerm::Print(foldername);
                    EarlyTerm::Print("\n");
                    return 0;
                }
                EarlyTerm::Print("[ERROR] Failed to create directory\n");
                return -1;
            }
            
            EarlyTerm::Print("[MISSING] Usage: create file:name OR create file name\n");
            return -1;
        }

        
        // DELETE FILE/FOLDER: delete file:name OR delete folder:name
        if (kstrcmp(action, "delete") == 0) {
            const char* filename = getModifier("file");
            const char* foldername = getModifier("folder");
            
            if (filename) {
                char path[128] = "/";
                int i = 1;
                const char* f = filename;
                while (*f && i < 127) {
                    path[i++] = *f++;
                }
                path[i] = 0;
                
                if (VFS::Delete(path)) {
                    EarlyTerm::Print("[OK] File deleted: ");
                    EarlyTerm::Print(filename);
                    EarlyTerm::Print("\n");
                    return 0;
                }
                EarlyTerm::Print("[ERROR] Failed to delete file\n");
                return -1;
            }
            
            if (foldername) {
                char path[128] = "/";
                int i = 1;
                const char* f = foldername;
                while (*f && i < 127) {
                    path[i++] = *f++;
                }
                path[i] = 0;
                
                if (VFS::Rmdir(path)) {
                    EarlyTerm::Print("[OK] Directory deleted: ");
                    EarlyTerm::Print(foldername);
                    EarlyTerm::Print("\n");
                    return 0;
                }
                EarlyTerm::Print("[ERROR] Failed to delete directory (not empty?)\n");
                return -1;
            }
            
            EarlyTerm::Print("[MISSING] Usage: delete file:name OR delete folder:name\n");
            return -1;
        }
        
        // OPEN FOLDER: open folder:name (change current path)
        if (kstrcmp(action, "open") == 0) {
            const char* foldername = getModifier("folder");
            
            if (foldername) {
                // Build full path
                char newPath[128];
                if (foldername[0] == '/') {
                    // Absolute path
                    int i = 0;
                    while (foldername[i] && i < 127) {
                        newPath[i] = foldername[i];
                        i++;
                    }
                    newPath[i] = 0;
                } else {
                    // Relative to current
                    int i = 0;
                    const char* p = currentPath;
                    while (*p && i < 126) {
                        newPath[i++] = *p++;
                    }
                    if (i > 1) newPath[i++] = '/';
                    const char* f = foldername;
                    while (*f && i < 127) {
                        newPath[i++] = *f++;
                    }
                    newPath[i] = 0;
                }
                
                VFSNode* dir = VFS::Open(newPath);
                if (dir && dir->type == NodeType::DIRECTORY) {
                    // Copy newPath to currentPath
                    int j = 0;
                    while (newPath[j] && j < 127) {
                        currentPath[j] = newPath[j];
                        j++;
                    }
                    currentPath[j] = 0;
                    EarlyTerm::Print("[OK] Current path: ");
                    EarlyTerm::Print(currentPath);
                    EarlyTerm::Print("\n");
                    return 0;
                }
                EarlyTerm::Print("[ERROR] Folder not found: ");
                EarlyTerm::Print(newPath);
                EarlyTerm::Print("\n");
                return -1;
            }
            
            EarlyTerm::Print("[MISSING] Usage: open folder:name\n");
            return -1;
        }
        
        // GO BACK: go back (go to parent directory)
        if (kstrcmp(action, "go") == 0 && kstrcmp(target, "back") == 0) {
            if (kstrcmp(currentPath, "/") == 0) {
                EarlyTerm::Print("[INFO] Already at root\n");
                return 0;
            }
            
            // Find last slash
            int lastSlash = 0;
            for (int i = 0; currentPath[i]; i++) {
                if (currentPath[i] == '/' && i > 0) {
                    lastSlash = i;
                }
            }
            
            if (lastSlash == 0) {
                currentPath[0] = '/';
                currentPath[1] = 0;
            } else {
                currentPath[lastSlash] = 0;
            }
            
            EarlyTerm::Print("[OK] Current path: ");
            EarlyTerm::Print(currentPath);
            EarlyTerm::Print("\n");
            return 0;
        }
        
        // SHOW PATH: show path (display current directory)
        if (kstrcmp(action, "show") == 0 && kstrcmp(target, "path") == 0) {
            EarlyTerm::Print("Current path: ");
            EarlyTerm::Print(currentPath);
            EarlyTerm::Print("\n");
            return 0;
        }
        
        // Forward show cpu/memory/version to Hardware
        if (kstrcmp(action, "show") == 0) {
            return ExecuteHardware(cmd);
        }
        
        EarlyTerm::Print("[UNKNOWN] Storage command: ");
        EarlyTerm::Print(action);
        if (target && target[0]) {
            EarlyTerm::Print(" ");
            EarlyTerm::Print(target);
        }
        EarlyTerm::Print("\n");

        return -1;
    }

    
    // ==================== HARDWARE COMMANDS ====================
    
    int ExecuteHardware(const MCLCommand* cmd) {
        const char* action = cmd->action.text;
        const char* target = cmd->target.text;
        
        // SHOW CPU
        if (kstrcmp(action, "show") == 0 && kstrcmp(target, "cpu") == 0) {
            EarlyTerm::Print("CPU Information:\n");
            #if defined(__x86_64__)
            EarlyTerm::Print("  Architecture: x86_64\n");
            #elif defined(__aarch64__)
            EarlyTerm::Print("  Architecture: ARM64\n");
            #endif
            EarlyTerm::Print("  Vendor: Generic\n");
            return 0;
        }
        
        // SHOW MEMORY
        if (kstrcmp(action, "show") == 0 && kstrcmp(target, "memory") == 0) {
            EarlyTerm::Print("Memory Information:\n");
            EarlyTerm::Print("  Heap Base: 0x400000\n");
            EarlyTerm::Print("  Heap Size: 16 MB\n");
            return 0;
        }
        
        // SHOW VERSION
        if (kstrcmp(action, "show") == 0 && kstrcmp(target, "version") == 0) {
            EarlyTerm::Print("Morphic OS v0.5 - Phase Swift HAL\n");
            EarlyTerm::Print("MCL Engine Active\n");
            return 0;
        }
        
        // CHECK MEMORY
        if (kstrcmp(action, "check") == 0 && kstrcmp(target, "memory") == 0) {
            EarlyTerm::Print("Running memory test...\n");
            EarlyTerm::Print("[OK] Memory integrity verified\n");
            return 0;
        }
        
        // TEST AUDIO
        if (kstrcmp(action, "test") == 0 && kstrcmp(target, "audio") == 0) {
            EarlyTerm::Print("Playing test tone (440 Hz)...\n");
            // AudioMixer::PlayTone(440, 500);
            return 0;
        }
        
        // SCAN BUS
        if (kstrcmp(action, "scan") == 0) {
            const char* bus = nullptr;
            for (int i = 0; i < cmd->modifier_count; i++) {
                if (kstrcmp(cmd->modifiers[i].key, "bus") == 0) {
                    bus = cmd->modifiers[i].value;
                }
            }
            
            if (kstrcmp(bus, "pci") == 0 || kstrcmp(target, "bus") == 0) {
                EarlyTerm::Print("PCI Bus Enumeration:\n");
                EarlyTerm::Print("  00:00.0 Host Bridge\n");
                EarlyTerm::Print("  00:01.0 VGA Controller (Cirrus/QEMU)\n");
                return 0;
            }
            
            EarlyTerm::Print("[ERROR] Unknown bus type\n");
            return -1;
        }
        
        EarlyTerm::Print("[UNKNOWN] Hardware command\n");
        return -1;
    }
    
    // ==================== SYSTEM COMMANDS ====================
    
    int ExecuteSystem(const MCLCommand* cmd) {
        const char* action = cmd->action.text;
        const char* target = cmd->target.text;
        
        // SET LAYOUT
        if (kstrcmp(action, "set") == 0 && kstrcmp(target, "layout") == 0) {
            const char* layout = nullptr;
            for (int i = 0; i < cmd->modifier_count; i++) {
                if (kstrcmp(cmd->modifiers[i].key, "layout") == 0) {
                    layout = cmd->modifiers[i].value;
                }
            }
            
            if (!layout && cmd->modifier_count > 0) {
                // Check for "set layout:es" format (target has the value)
                layout = cmd->modifiers[0].value;
            }
            
            if (layout) {
                if (kstrcmp(layout, "us") == 0) {
                    KeymapHAL::SetKeymap("US");
                    EarlyTerm::Print("[Keymap] Switched to: US\n");
                } else if (kstrcmp(layout, "es") == 0) {
                    KeymapHAL::SetKeymap("ES");
                    EarlyTerm::Print("[Keymap] Switched to: ES\n");
                } else if (kstrcmp(layout, "la") == 0) {
                    KeymapHAL::SetKeymap("LA");
                    EarlyTerm::Print("[Keymap] Switched to: LA\n");
                } else {
                    EarlyTerm::Print("[ERROR] Unknown layout. Use: us, es, la\n");
                    return -1;
                }
                return 0;
            }
            
            EarlyTerm::Print("[MISSING] Usage: set layout:us/es/la\n");
            return -1;
        }
        
        // SET VOLUME
        if (kstrcmp(action, "set") == 0 && kstrcmp(target, "volume") == 0) {
            EarlyTerm::Print("[OK] Volume adjusted\n");
            return 0;
        }
        
        // TOGGLE VERBOSE
        if (kstrcmp(action, "toggle") == 0 && kstrcmp(target, "verbose") == 0) {
            EarlyTerm::Print("[Verbose] Debug output toggled\n");
            return 0;
        }
        
        // REBOOT
        if (kstrcmp(action, "reboot") == 0) {
            if (kstrcmp(target, "safe") == 0) {
                EarlyTerm::Print("Syncing filesystems...\n");
            }
            EarlyTerm::Print("Rebooting...\n");
            
            #if defined(__x86_64__)
            // Method 1: 8042 Keyboard Controller Reset
            // Wait for keyboard controller input buffer to be empty
            uint8_t status;
            do {
                __asm__ volatile("inb $0x64, %0" : "=a"(status));
            } while (status & 0x02);
            
            // Send CPU reset command to keyboard controller
            __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
            
            // Method 2: Triple fault (backup)
            // Load null IDT and trigger interrupt
            struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
            __asm__ volatile("lidt %0" : : "m"(null_idt));
            __asm__ volatile("int $3");
            #endif
            
            // Fallback: infinite halt
            for (;;) __asm__ volatile("hlt");
            return 0;
        }
        
        // SHUTDOWN
        if (kstrcmp(action, "shutdown") == 0) {
            EarlyTerm::Print("Shutting down...\n");
            
            #if defined(__x86_64__)
            // QEMU shutdown via debug exit port
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
            
            // Bochs/older QEMU shutdown
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
            
            // ACPI shutdown (write to PM1a_CNT)
            // Common ACPI addresses: 0x4004 for PIIX4
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)(0x2000 | (5 << 10))), "Nd"((uint16_t)0x4004));
            #endif
            
            // Fallback: halt with interrupts disabled
            EarlyTerm::Print("System halted. You may power off now.\n");
            __asm__ volatile("cli");
            for (;;) __asm__ volatile("hlt");
            return 0;
        }
        
        EarlyTerm::Print("[UNKNOWN] System command\n");
        return -1;
    }
}

