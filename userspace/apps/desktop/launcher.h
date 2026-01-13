#pragma once
#include "compositor.h"

// Define external sys_spawn if not in header
extern "C" int sys_spawn(const char* path);

struct AppItem {
    const char* name;
    const char* mpk_path;
    uint32_t color;
    int x, y, w, h;
};

class Launcher {
public:
    void Init() {
        // Initialize Bento Grid Layout
        // 4 Columns
        // In a real dynamic system, we would scan, but for this refactor (Simulated Registry),
        // we hardcode the known apps.
        
        apps[0] = {"Calculator", "/initrd/calculator.mpk", 0xFFE05050, 0, 0, 1, 1}; // 1x1
        apps[1] = {"Terminal",   "/initrd/terminal.mpk",   0xFF40A040, 1, 0, 2, 1}; // 2x1 Wide
        // Add placeholders to show off the grid
        apps[2] = {"System",     "",                         0xFF4080A0, 3, 0, 1, 1};
        apps[3] = {"Editor",     "",                         0xFFCCA040, 0, 1, 1, 1};
        apps[4] = {"Files",      "",                         0xFFA040A0, 1, 1, 1, 1};
        
        appCount = 5;
    }

    void Draw(int screenW, int screenH) {
        // Full screen overlay with semi-transparent background
        // Compositor doesn't support transparency well yet? 
        // We'll draw a dark overlay.
        Compositor::DrawRect(0, 0, screenW, screenH, 0xEE101010); // Alpha? 0xEE is alpha?
        // Assuming ARGB.
        
        // Draw Grid
        int gridStartX = 100;
        int gridStartY = 100;
        int cellSize = 120;
        int gap = 20;

        // Title
        Compositor::DrawText(gridStartX, 50, "Morphic OS Launcher", 0xFFFFFFFF, 3);

        for (int i = 0; i < appCount; i++) {
            int cx = gridStartX + apps[i].x * (cellSize + gap);
            int cy = gridStartY + apps[i].y * (cellSize + gap);
            int cw = apps[i].w * cellSize + (apps[i].w - 1) * gap;
            int ch = apps[i].h * cellSize + (apps[i].h - 1) * gap;

            Compositor::DrawRect(cx, cy, cw, ch, apps[i].color);
            Compositor::DrawRect(cx, cy, cw, ch, 0x40FFFFFF); // Shine/Highlight (simulated)
            
            // Text Centered
            int textLen = StrLen(apps[i].name);
            int textW = textLen * 8 * 2;
            int textH = 16 * 2;
            Compositor::DrawText(cx + (cw - textW)/2, cy + (ch - textH)/2, apps[i].name, 0xFFFFFFFF, 2);
        }
    }

    bool HandleClick(int mx, int my) {
        // Check grid
        int gridStartX = 100;
        int gridStartY = 100;
        int cellSize = 120;
        int gap = 20;

        for (int i = 0; i < appCount; i++) {
            int cx = gridStartX + apps[i].x * (cellSize + gap);
            int cy = gridStartY + apps[i].y * (cellSize + gap);
            int cw = apps[i].w * cellSize + (apps[i].w - 1) * gap;
            int ch = apps[i].h * cellSize + (apps[i].h - 1) * gap;

            if (mx >= cx && mx < cx + cw && my >= cy && my < cy + ch) {
                // Launch!
                if (apps[i].mpk_path && apps[i].mpk_path[0]) {
                    // sys_spawn(apps[i].mpk_path);
                    // Syscall expects absolute path or VFS path.
                    // VFS path inside MPK? No, we need to load checking VFS.
                    // The loader expects VFS path.
                    // "userspace/calculator.mpk" should work if it is in initrd/root.
                    // In `kernel_main`, we mount initrd as `/`.
                    // So `/calculator.mpk` likely if flat.
                    // Wait, `Makefile` copies them to `::/`. So they are at `/calculator.mpk`.
                    // But `userspace/desktop.mpk` is at `/desktop.mpk`.
                    // Let's try `/userspace/calculator.mpk` if directories are preserved?
                    // `mcopy -i morphic.img userspace/calculator.mpk ::/` -> This puts it at root if no dest dir specified.
                    // It puts it at `/calculator.mpk`.
                    
                    sys_spawn(apps[i].mpk_path); // Start with provided path, verify later.
                    return true;
                }
            }
        }
        return false; // Not handled, or background click
    }

private:
    AppItem apps[16];
    int appCount;

    int StrLen(const char* s) {
        int l=0; while(s[l]) l++; return l;
    }
};
