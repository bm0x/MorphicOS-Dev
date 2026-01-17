#pragma once
#include "compositor.h"
#include "../../sdk/morphic_syscalls.h"

// Define external sys_spawn if not in header
extern "C" int sys_spawn(const char* path);

struct AppItem {
    char name[32];
    char mpk_path[64];
    uint32_t color;
    int x, y, w, h;
};

class Launcher {
public:
    bool spawnPending;  // Prevent multiple spawns
    
    void Init() {
        spawnPending = false;
        // Scan /initrd/ for .mpk files dynamically
        ScanApps();
    }
    
    void ScanApps() {
        appCount = 0;
        
        // Scan initrd for installed apps
        DirEntry entries[32];
        int count = sys_readdir("/initrd", entries, 32);
        
        for (int i = 0; i < count && appCount < 16; i++) {
            // Check if it's an .mpk file
            if (entries[i].type == 0) { // File
                if (EndsWith(entries[i].name, ".mpk")) {
                    // Skip desktop.mpk - launching multiple compositors causes kernel panic
                    if (StrCmp(entries[i].name, "desktop.mpk") == 0) continue;
                    
                    // Add to apps list
                    BuildAppItem(entries[i].name, "/initrd/");
                }
            }
        }
        
        // TODO: Future - also scan /usr/apps/ for installed apps from debug_disk
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
            
            // Icon Background color based on app name hash
            uint32_t iconColor = GetAppColor(apps[i].name);
            
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
        int pages = 1; // Dynamic based on app count
        int totalDotsW = (pages * dotSize) + ((pages - 1) * dotGap);
        int dotX = (screenW - totalDotsW) / 2;
        
        // Page 1 (Active) - White
        Compositor::DrawRect(dotX, dotsY, dotSize, dotSize, 0xFFFFFFFF);
        
        // Power Buttons (Bottom Right)
        int btnW = 70;
        int btnH = 28;
        int btnY = screenH - 80;
        int btnGap = 10;
        
        // Shutdown button (red)
        int shutdownX = screenW - btnW - 20;
        Compositor::DrawRect(shutdownX, btnY, btnW, btnH, 0xE0B02020);
        Compositor::DrawText(shutdownX + 8, btnY + 8, "Apagar", 0xFFFFFFFF, 1);
        
        // Reboot button (blue)
        int rebootX = shutdownX - btnW - btnGap;
        Compositor::DrawRect(rebootX, btnY, btnW, btnH, 0xE02060A0);
        Compositor::DrawText(rebootX + 6, btnY + 8, "Reiniciar", 0xFFFFFFFF, 1);
    }

    bool HandleClick(int mx, int my) {
        int screenW = Compositor::GetWidth();
        int screenH = Compositor::GetHeight();
        
        // Power Buttons hit test
        int btnW = 70;
        int btnH = 28;
        int btnY = screenH - 80;
        int btnGap = 10;
        
        int shutdownX = screenW - btnW - 20;
        int rebootX = shutdownX - btnW - btnGap;
        
        // Shutdown button
        if (mx >= shutdownX && mx < shutdownX + btnW && my >= btnY && my < btnY + btnH) {
            sys_shutdown();
            return true;
        }
        
        // Reboot button  
        if (mx >= rebootX && mx < rebootX + btnW && my >= btnY && my < btnY + btnH) {
            sys_reboot();
            return true;
        }
        
        // App grid hit test
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
                // Launch! But only if not already spawning
                if (apps[i].mpk_path[0] && !spawnPending) {
                    spawnPending = true;  // Prevent double-click spawns
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
    
    bool EndsWith(const char* str, const char* suffix) {
        int strLen = StrLen(str);
        int sufLen = StrLen(suffix);
        if (sufLen > strLen) return false;
        
        for (int i = 0; i < sufLen; i++) {
            if (str[strLen - sufLen + i] != suffix[i]) return false;
        }
        return true;
    }
    
    int StrCmp(const char* a, const char* b) {
        while (*a && *b && *a == *b) { a++; b++; }
        return *a - *b;
    }
    
    void BuildAppItem(const char* filename, const char* basePath) {
        AppItem& app = apps[appCount];
        
        // Build full path
        int i = 0;
        const char* p = basePath;
        while (*p && i < 63) app.mpk_path[i++] = *p++;
        p = filename;
        while (*p && i < 63) app.mpk_path[i++] = *p++;
        app.mpk_path[i] = 0;
        
        // Extract name (remove .mpk extension and capitalize first letter)
        i = 0;
        while (filename[i] && filename[i] != '.' && i < 31) {
            app.name[i] = filename[i];
            i++;
        }
        app.name[i] = 0;
        
        // Capitalize first letter
        if (app.name[0] >= 'a' && app.name[0] <= 'z') {
            app.name[0] = app.name[0] - 'a' + 'A';
        }
        
        app.color = GetAppColor(app.name);
        app.x = appCount % 5;
        app.y = appCount / 5;
        app.w = 1;
        app.h = 1;
        
        appCount++;
    }
    
    uint32_t GetAppColor(const char* name) {
        // Simple hash-based color
        uint32_t hash = 0;
        while (*name) {
            hash = hash * 31 + *name;
            name++;
        }
        
        // Generate color from hash (avoid too bright or too dark)
        uint8_t r = 40 + (hash % 160);
        uint8_t g = 40 + ((hash >> 8) % 160);
        uint8_t b = 40 + ((hash >> 16) % 160);
        
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
};
