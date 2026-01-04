#include "shell.h"
#include "../hal/video/early_term.h"
#include "../hal/video/graphics.h"
#include "../hal/video/verbose.h"
#include "../hal/video/compositor.h"
#include "../hal/input/mouse.h"
#include "../hal/input/keymap.h"
#include "../hal/audio/audio_device.h"
#include "../drivers/keyboard.h"
#include "../utils/std.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../fs/vfs.h"
#include "../hal/storage/block_device.h"
#include "../hal/storage/buffer_cache.h"
#include "../mcl/mcl_parser.h"
#include "../gui/desktop.h"
#include "loader.h"
#include "../process/scheduler.h"

namespace Shell {
    const int CMD_BUFFER_SIZE = 128;
    char cmdBuffer[CMD_BUFFER_SIZE];
    int cmdLen = 0;
    
    // Helper: Get argument from command (after first space)
    const char* GetArg(const char* cmd) {
        while (*cmd && *cmd != ' ') cmd++;
        if (*cmd == ' ') cmd++;
        return (*cmd) ? cmd : nullptr;
    }
    
    // Helper: Check if command starts with prefix
    bool StartsWith(const char* str, const char* prefix) {
        while (*prefix) {
            if (*str != *prefix) return false;
            str++;
            prefix++;
        }
        return true;
    }
    
    // Command Handlers
    void CmdHelp() {
        EarlyTerm::Print("=== Morphic Shell Commands ===\n\n");
        
        EarlyTerm::Print("Legacy Commands:\n");
        EarlyTerm::Print("  help      - Show this help\n");
        EarlyTerm::Print("  info      - System status\n");
        EarlyTerm::Print("  clear     - Clear screen\n");
        EarlyTerm::Print("  ls        - List files (root)\n");
        EarlyTerm::Print("  cat <f>   - Show file content\n");
        EarlyTerm::Print("  keymap <L>- Set keyboard (US/ES/LA)\n");
        EarlyTerm::Print("  beep      - Test PC speaker\n");
        EarlyTerm::Print("  comptest  - Compositor demo\n");
        EarlyTerm::Print("  desktop   - Launch GUI desktop\n\n");
        
        EarlyTerm::Print("=== MCL (Natural Language) ===\n\n");
        
        EarlyTerm::Print("Navigation:\n");
        EarlyTerm::Print("  show path           - Current directory\n");
        EarlyTerm::Print("  open folder:<name>  - Enter folder\n");
        EarlyTerm::Print("  go back             - Parent directory\n\n");
        
        EarlyTerm::Print("Storage:\n");
        EarlyTerm::Print("  list files          - List files only\n");
        EarlyTerm::Print("  list folders        - List folders only\n");
        EarlyTerm::Print("  read file:<name>    - Show file content\n");
        EarlyTerm::Print("  create file:<name>  - Create empty file\n");
        EarlyTerm::Print("  delete file:<name>  - Delete file\n");
        EarlyTerm::Print("  create folder:<n>   - Create directory\n");
        EarlyTerm::Print("  delete folder:<n>   - Remove directory\n\n");
        
        EarlyTerm::Print("Hardware:\n");
        EarlyTerm::Print("  show cpu            - CPU information\n");
        EarlyTerm::Print("  show memory         - RAM status\n");
        EarlyTerm::Print("  show version        - OS version\n");
        EarlyTerm::Print("  check memory        - Memory test\n");
        EarlyTerm::Print("  test audio          - Audio test tone\n");
        EarlyTerm::Print("  scan bus:pci        - PCI devices\n\n");
        
        EarlyTerm::Print("System:\n");
        EarlyTerm::Print("  set layout:<code>   - Keyboard (us/es/la)\n");
        EarlyTerm::Print("  set volume:<0-100>  - Audio volume\n");
        EarlyTerm::Print("  toggle verbose      - Debug output\n");
        EarlyTerm::Print("  reboot now|safe     - Restart system\n");
        EarlyTerm::Print("  shutdown now        - Power off\n");
    }



    
    void CmdBeep() {
        EarlyTerm::Print("Beeping...\n");
        Audio::Beep(440, 200);   // A4 note, 200ms
        Audio::Beep(523, 200);   // C5 note, 200ms
        Audio::Beep(659, 200);   // E5 note, 200ms
        Audio::Beep(784, 400);   // G5 note, 400ms
        EarlyTerm::Print("Done.\n");
    }
    
    void CmdKeymap(const char* layout) {
        if (!layout || !*layout) {
            EarlyTerm::Print("Current keymap: ");
            const Keymap* km = KeymapHAL::GetCurrentKeymap();
            if (km) EarlyTerm::Print(km->id);
            EarlyTerm::Print("\nAvailable: US, ES, LA\n");
            return;
        }
        
        if (KeymapHAL::SetKeymap(layout)) {
            EarlyTerm::Print("Keymap set to: ");
            EarlyTerm::Print(layout);
            EarlyTerm::Print("\n");
        } else {
            EarlyTerm::Print("Unknown keymap: ");
            EarlyTerm::Print(layout);
            EarlyTerm::Print("\n");
        }
    }
    
    void CmdInfo() {
        EarlyTerm::Print("Morphic OS v0.5 (Swift HAL)\n");
        EarlyTerm::Print("VFS: InitRD at /\n");
        EarlyTerm::Print("HAL: Input, Video, Storage\n");
        EarlyTerm::Print("Block Devices: ");
        EarlyTerm::PrintDec(StorageManager::GetDeviceCount());
        EarlyTerm::Print("\nBuffer Cache: ");
        EarlyTerm::PrintDec(BufferCache::GetHitCount());
        EarlyTerm::Print(" hits, ");
        EarlyTerm::PrintDec(BufferCache::GetMissCount());
        EarlyTerm::Print(" misses\n");
        EarlyTerm::Print("Free RAM: ");
        EarlyTerm::PrintDec(PMM::GetFreeMemory() / 1024);
        EarlyTerm::Print(" KB\n");
    }
    
    void CmdClear() {
        EarlyTerm::Clear();
    }
    
    void CmdLs() {
        VFSNode* root = VFS::GetRoot();
        if (!root) {
            EarlyTerm::Print("Error: No filesystem mounted.\n");
            return;
        }
        
        uint32_t count = 0;
        VFSNode** files = VFS::ListDir(root, &count);
        
        if (count == 0) {
            EarlyTerm::Print("(empty directory)\n");
            return;
        }
        
        for (uint32_t i = 0; i < count; i++) {
            if (files[i]->type == NodeType::DIRECTORY) {
                EarlyTerm::Print("[DIR]  ");
            } else {
                EarlyTerm::Print("       ");
            }
            EarlyTerm::Print(files[i]->name);
            if (files[i]->type == NodeType::FILE) {
                EarlyTerm::Print(" (");
                EarlyTerm::PrintDec(files[i]->size);
                EarlyTerm::Print(" bytes)");
            }
            EarlyTerm::Print("\n");
        }
        
        kfree(files);
    }
    
    void CmdCat(const char* filename) {
        if (!filename || *filename == 0) {
            EarlyTerm::Print("Usage: cat <filename>\n");
            return;
        }
        
        // Build path
        char path[128];
        if (filename[0] == '/') {
            // Absolute path
            int i = 0;
            while (filename[i] && i < 127) {
                path[i] = filename[i];
                i++;
            }
            path[i] = 0;
        } else {
            // Relative to root
            path[0] = '/';
            int i = 0;
            while (filename[i] && i < 126) {
                path[i + 1] = filename[i];
                i++;
            }
            path[i + 1] = 0;
        }
        
        VFSNode* node = VFS::Open(path);
        if (!node) {
            EarlyTerm::Print("Error: File not found: ");
            EarlyTerm::Print(path);
            EarlyTerm::Print("\n");
            return;
        }
        
        if (node->type != NodeType::FILE) {
            EarlyTerm::Print("Error: Not a file.\n");
            return;
        }
        
        // Read and print content
        uint8_t buffer[512];
        uint32_t offset = 0;
        uint32_t bytesRead;
        
        while ((bytesRead = VFS::Read(node, offset, 511, buffer)) > 0) {
            buffer[bytesRead] = 0; // Null terminate
            EarlyTerm::Print((const char*)buffer);
            offset += bytesRead;
        }
    }
    
    void CmdTouch(const char* filename) {
        if (!filename || *filename == 0) {
            EarlyTerm::Print("Usage: touch <filename>\n");
            return;
        }
        
        // Build path
        char path[128];
        if (filename[0] == '/') {
            int i = 0;
            while (filename[i] && i < 127) {
                path[i] = filename[i];
                i++;
            }
            path[i] = 0;
        } else {
            path[0] = '/';
            int i = 0;
            while (filename[i] && i < 126) {
                path[i + 1] = filename[i];
                i++;
            }
            path[i + 1] = 0;
        }
        
        VFSNode* node = VFS::CreateFile(path);
        if (node) {
            EarlyTerm::Print("Created: ");
            EarlyTerm::Print(node->name);
            EarlyTerm::Print("\n");
        } else {
            EarlyTerm::Print("Error: Cannot create file (exists or invalid path).\n");
        }
    }
    
    void CmdDevList() {
        EarlyTerm::Print("-- Block Devices --\n");
        for (uint32_t i = 0; i < StorageManager::GetDeviceCount(); i++) {
            IBlockDevice* dev = StorageManager::GetDeviceByIndex(i);
            if (dev) {
                EarlyTerm::Print("  ");
                EarlyTerm::Print(dev->name);
                EarlyTerm::Print(" (");
                EarlyTerm::PrintDec(dev->geometry.total_bytes / 1024);
                EarlyTerm::Print(" KB)\n");
            }
        }
    }
    
    void CmdBlkTest() {
        IBlockDevice* dev = StorageManager::GetDevice("rd0");
        if (!dev) {
            EarlyTerm::Print("No rd0 device found.\n");
            return;
        }
        
        EarlyTerm::Print("Testing RAMDisk rd0...\n");
        
        // Write test
        uint8_t write_buf[512];
        kmemset(write_buf, 0, 512);
        kmemcpy(write_buf, "Morphic Block Test!", 20);
        
        if (StorageManager::WriteSectors("rd0", 0, 1, write_buf)) {
            EarlyTerm::Print("Write OK.\n");
        } else {
            EarlyTerm::Print("Write FAILED.\n");
            return;
        }
        
        // Read test
        uint8_t read_buf[512];
        kmemset(read_buf, 0, 512);
        
        if (StorageManager::ReadSectors("rd0", 0, 1, read_buf)) {
            EarlyTerm::Print("Read OK: ");
            EarlyTerm::Print((const char*)read_buf);
            EarlyTerm::Print("\n");
        } else {
            EarlyTerm::Print("Read FAILED.\n");
        }
    }
    
    void CmdPanic() {
        EarlyTerm::Print("Triggering test Kernel Panic...\n");
        volatile int x = 0;
        volatile int y = 1 / x;
        (void)y;
    }
    
    void CmdLogoTest() {
        EarlyTerm::Print("Displaying boot logo...\n");
        
        // Clear with dark background
        Graphics::Clear(0xFF1a1a2e);
        
        // Draw Morphic logo (procedural - no file needed)
        uint32_t cx = Graphics::GetWidth() / 2;
        uint32_t cy = Graphics::GetHeight() / 2;
        
        // Outer ring (cyan)
        for (int y = -40; y <= 40; y++) {
            for (int x = -40; x <= 40; x++) {
                int d = x*x + y*y;
                if (d >= 1200 && d <= 1600) {
                    Graphics::PutPixel(cx + x, cy + y, 0xFF00FFFF);
                }
            }
        }
        
        // Inner M shape (white)
        Graphics::FillRect(cx - 30, cy - 15, 8, 30, COLOR_WHITE);
        Graphics::FillRect(cx + 22, cy - 15, 8, 30, COLOR_WHITE);
        Graphics::FillRect(cx - 22, cy - 15, 4, 15, COLOR_WHITE);
        Graphics::FillRect(cx + 18, cy - 15, 4, 15, COLOR_WHITE);
        Graphics::FillRect(cx - 4, cy - 5, 8, 20, COLOR_WHITE);
        
        // Text: "MORPHIC OS"
        Graphics::FillRect(cx - 60, cy + 55, 120, 2, 0xFF00FFFF);
        
        // Flip to screen
        Graphics::Flip();
        
        EarlyTerm::Print("Press any key to return...\n");
        // Wait for keypress
        while (Keyboard::GetChar() == 0);
        
        // Restore text mode
        EarlyTerm::Clear();
        EarlyTerm::Print("Logo test complete.\n");
    }
    
    void CmdMouseTest() {
        EarlyTerm::Print("Mouse test - move mouse, any key to exit\n");
        
        // Initial clear and setup
        Graphics::Clear(0xFF2d2d44);
        Graphics::FillRect(10, 10, 250, 25, 0xFF404060);
        Graphics::Flip();
        
        Mouse::SetCursorVisible(true);
        
        // Track previous cursor position for dirty rect
        int16_t prevX = Mouse::GetX();
        int16_t prevY = Mouse::GetY();
        
        // Poll loop until keypress
        while (Keyboard::GetChar() == 0) {
            int16_t curX = Mouse::GetX();
            int16_t curY = Mouse::GetY();
            
            // Only redraw if mouse moved
            if (curX != prevX || curY != prevY) {
                // Erase old cursor (draw background over it)
                Graphics::FillRect(prevX, prevY, 10, 10, 0xFF2d2d44);
                
                // Draw new cursor
                Mouse::DrawCursor();
                
                // Only flip the changed region would be ideal,
                // but for now just flip
                Graphics::Flip();
                
                prevX = curX;
                prevY = curY;
            }
            
            // Small delay to reduce CPU usage
            for (volatile int i = 0; i < 50000; i++);
        }
        
        Mouse::SetCursorVisible(false);
        
        EarlyTerm::Clear();
        EarlyTerm::Print("Mouse test complete. Final pos: ");
        EarlyTerm::PrintDec(Mouse::GetX());
        EarlyTerm::Print(", ");
        EarlyTerm::PrintDec(Mouse::GetY());
        EarlyTerm::Print("\n");
    }
    
    void CmdCompTest() {
        EarlyTerm::Print("Compositor demo - F12 toggles overlay, any key exits\n");
        
        // Create a window layer
        Layer* window = Compositor::CreateLayer("testwin", LayerType::WINDOW, 200, 150);
        if (!window) {
            EarlyTerm::Print("Failed to create window layer\n");
            return;
        }
        
        window->x = 100;
        window->y = 80;
        
        // Fill window with gradient
        for (uint32_t y = 0; y < 150; y++) {
            for (uint32_t x = 0; x < 200; x++) {
                uint32_t color = 0xFF000000 | (100 + x/2) << 16 | (80 + y/2) << 8 | 180;
                window->buffer[y * 200 + x] = color;
            }
        }
        
        // Title bar
        for (uint32_t y = 0; y < 20; y++) {
            for (uint32_t x = 0; x < 200; x++) {
                window->buffer[y * 200 + x] = 0xFF4060A0;
            }
        }
        
        // Update cursor layer position
        Layer* cursor = Compositor::GetCursorLayer();
        
        while (Keyboard::GetChar() == 0) {
            // Update cursor position
            if (cursor) {
                cursor->x = Mouse::GetX();
                cursor->y = Mouse::GetY();
            }
            
            // Compose and flip
            Compositor::Compose();
            Compositor::Flip();
            
            for (volatile int i = 0; i < 50000; i++);
        }
        
        // Cleanup
        Compositor::DestroyLayer(window);
        
        EarlyTerm::Clear();
        EarlyTerm::Print("Compositor test complete.\n");
    }

    void CmdDesktop() {
        EarlyTerm::Print("Starting Morphic Desktop Environment (.mpk)...\n");
        // Use PackageLoader to load the structured application container
        LoadedProcess proc = PackageLoader::Load("/initrd/desktop.mpk");
        if (proc.error_code == 0) {
            Scheduler::CreateUserTask((void(*)())proc.entry_point, (void*)proc.stack_top);
        } else {
            EarlyTerm::Print("Error: Failed to load desktop.mpk\n");
        }
    }


    void ExecuteCommand() {

        EarlyTerm::Print("\n");
        
        if (cmdLen == 0) return;
        
        if (kstrcmp(cmdBuffer, "help") == 0) {
            CmdHelp();
        } else if (kstrcmp(cmdBuffer, "info") == 0) {
            CmdInfo();
        } else if (kstrcmp(cmdBuffer, "clear") == 0) {
            CmdClear();
        } else if (kstrcmp(cmdBuffer, "ls") == 0) {
            CmdLs();
        } else if (StartsWith(cmdBuffer, "cat ")) {
            CmdCat(GetArg(cmdBuffer));
        } else if (StartsWith(cmdBuffer, "touch ")) {
            CmdTouch(GetArg(cmdBuffer));
        } else if (kstrcmp(cmdBuffer, "devlist") == 0) {
            CmdDevList();
        } else if (kstrcmp(cmdBuffer, "blktest") == 0) {
            CmdBlkTest();
        } else if (kstrcmp(cmdBuffer, "logotest") == 0) {
            CmdLogoTest();
        } else if (kstrcmp(cmdBuffer, "mousetest") == 0) {
            CmdMouseTest();
        } else if (kstrcmp(cmdBuffer, "comptest") == 0) {
            CmdCompTest();
        } else if (kstrcmp(cmdBuffer, "beep") == 0) {
            CmdBeep();
        } else if (kstrcmp(cmdBuffer, "desktop") == 0) {
            CmdDesktop();
        } else if (kstrcmp(cmdBuffer, "keymap") == 0) {


            CmdKeymap(nullptr);
        } else if (StartsWith(cmdBuffer, "keymap ")) {
            CmdKeymap(GetArg(cmdBuffer));
        } else if (kstrcmp(cmdBuffer, "desktop") == 0) {
            EarlyTerm::Print("Launching desktop...\n");
            Desktop::Run();
            EarlyTerm::Print("Returned to shell.\n");
        } else if (kstrcmp(cmdBuffer, "panic") == 0) {
            CmdPanic();
        } else {

            // Try MCL (Natural Language Commands)
            MCLCommand mclCmd = MCL::Parse(cmdBuffer);
            if (mclCmd.valid) {
                MCL::Execute(&mclCmd);
            } else {
                EarlyTerm::Print("Unknown command: ");
                EarlyTerm::Print(cmdBuffer);
                EarlyTerm::Print("\nType 'help' or use MCL syntax: list files, show cpu, etc.\n");
            }
        }

    }

    void Init() {
        cmdLen = 0;
        kmemset(cmdBuffer, 0, CMD_BUFFER_SIZE);
        EarlyTerm::Print("\nuser@morphic:/$ ");
    }

    void OnChar(char c) {
        if (c == '\n') {
            cmdBuffer[cmdLen] = 0;
            ExecuteCommand();
            cmdLen = 0;
            kmemset(cmdBuffer, 0, CMD_BUFFER_SIZE);
            EarlyTerm::Print("user@morphic:/$ ");
        } else if (c == '\b') {
            if (cmdLen > 0) {
                cmdLen--;
                cmdBuffer[cmdLen] = 0;
                EarlyTerm::PutChar('\b');
            }
        } else {
            if (cmdLen < CMD_BUFFER_SIZE - 1) {
                cmdBuffer[cmdLen++] = c;
                cmdBuffer[cmdLen] = 0;
                EarlyTerm::PutChar(c);
            }
        }
    }
}
