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
        // Full screen overlay
        // Dark background (matches macOS Launchpad blur/darkening)
        Compositor::DrawRect(0, 0, screenW, screenH, 0xD0000000); 
        
        // Search Bar (Rounded Rect approximation)
        int searchW = 300;
        int searchH = 34;
        int searchX = (screenW - searchW) / 2;
        int searchY = 60;
        Compositor::DrawRect(searchX, searchY, searchW, searchH, 0x40FFFFFF); 
        // Search Text
        const char* sTxt = "Search";
        Compositor::DrawText(searchX + 110, searchY + 9, sTxt, 0x80FFFFFF, 1);
        // Magnifying glass icon simulation (simple circle)
         // Compositor::DrawRect(searchX + 10, searchY + 10, 14, 14, 0xFFFFFFFF);

        // App Grid
        // 5 columns, centered
        int cols = 5;
        int iconSize = 80; // Standard icon size
        int gapX = 60;
        int gapY = 60;
        
        int totalRowW = (cols * iconSize) + ((cols - 1) * gapX);
        int gridStartX = (screenW - totalRowW) / 2;
        int gridStartY = 160;

        for (int i = 0; i < appCount; i++) {
            int col = i % cols;
            int row = i / cols;
            
            int cx = gridStartX + col * (iconSize + gapX);
            int cy = gridStartY + row * (iconSize + gapY);
            
            // Icon Background (Rounded Rect Shape)
            // Color mapping based on name to match style
            uint32_t iconColor = apps[i].color;
            if (apps[i].name[0] == 'C') iconColor = 0xFF404040; // Calc (Grey)
            if (apps[i].name[0] == 'T') iconColor = 0xFF202020; // Term (Black)
            if (apps[i].name[0] == 'S') iconColor = 0xFF4080A0; // System (Blue)
            
            Compositor::DrawRect(cx, cy, iconSize, iconSize, iconColor);
            
            // Icon Gloss/Gradient (Top half lighter)
            Compositor::DrawRect(cx, cy, iconSize, iconSize/2, 0x20FFFFFF);
            
            // Icon Label
            int textLen = StrLen(apps[i].name);
            int textW = textLen * 8; // Font size 1
            int textX = cx + (iconSize - textW) / 2;
            int textY = cy + iconSize + 10;
            
            Compositor::DrawText(textX, textY, apps[i].name, 0xFFE0E0E0, 1);
        }
        
        // Pagination Dots (Bottom Center)
        int dotsY = screenH - 120; // Above dock
        int dotSize = 8;
        int dotGap = 12;
        int pages = 2; // Simulate 2 pages
        int totalDotsW = (pages * dotSize) + ((pages - 1) * dotGap);
        int dotX = (screenW - totalDotsW) / 2;
        
        // Page 1 (Active) - White
        Compositor::DrawRect(dotX, dotsY, dotSize, dotSize, 0xFFFFFFFF);
        // Page 2 (Inactive) - Gray
        Compositor::DrawRect(dotX + dotSize + dotGap, dotsY, dotSize, dotSize, 0x80FFFFFF);

    }

    bool HandleClick(int mx, int my) {
        // Redefine grid constants to match Draw
        int screenW = Compositor::GetWidth();
        int cols = 5;
        int iconSize = 80; 
        int gapX = 60;
        int gapY = 60;
        
        int totalRowW = (cols * iconSize) + ((cols - 1) * gapX);
        int gridStartX = (screenW - totalRowW) / 2;
        int gridStartY = 160;

        for (int i = 0; i < appCount; i++) {
            int col = i % cols;
            int row = i / cols;

            int cx = gridStartX + col * (iconSize + gapX);
            int cy = gridStartY + row * (iconSize + gapY);
            int cw = iconSize;
            int ch = iconSize;

            if (mx >= cx && mx < cx + cw && my >= cy && my < cy + ch) {
                // Launch!
                if (apps[i].mpk_path && apps[i].mpk_path[0]) {
                    // Try absolute path launching first (standard for initrd)
                    sys_spawn(apps[i].mpk_path); 
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
