#pragma once
#include "compositor.h"
#include "../../sdk/morphic_syscalls.h"

extern "C" int sys_spawn(const char* path);

struct AppItem {
    char name[32];
    char mpk_path[64];
    uint32_t colorTop;
    uint32_t colorBot;
    int x, y, w, h;
};

class Launcher {
public:
    bool spawnPending;

    void Init() {
        spawnPending = false;
        searchLen = 0;
        searchQuery[0] = '\0';
        ScanApps();
    }

    void ResetSearch() {
        searchLen = 0;
        searchQuery[0] = '\0';
    }

    // Returns 0=ignored, 1=query updated, 2=app launched via Enter
    int HandleKey(char c) {
        if (c == '\b' || c == 127) {
            if (searchLen > 0) {
                searchQuery[--searchLen] = '\0';
                return 1;
            }
            return 0;
        }
        if (c == '\n' || c == '\r') {
            for (int i = 0; i < appCount; i++) {
                if (MatchesQuery(apps[i].name) && apps[i].mpk_path[0] && !spawnPending) {
                    spawnPending = true;
                    sys_spawn(apps[i].mpk_path);
                    ResetSearch();
                    return 2;
                }
            }
            return 0;
        }
        if (c >= 32 && c <= 126 && searchLen < 28) {
            searchQuery[searchLen++] = c;
            searchQuery[searchLen] = '\0';
            return 1;
        }
        return 0;
    }

    void ScanApps() {
        appCount = 0;
        ScanDirectory("/initrd");
        ScanDirectory("/data/apps");
    }

    void Draw(int screenW, int screenH) {
        // Fast opaque Glass Dark gradient backdrop (rendered cleanly into BACK_BUFFER, zero flicker)
        Compositor::DrawGradientV(0, 0, screenW, screenH, 0xFF0A1122, 0xFF0E162B);
        Compositor::DrawRect(0, 28, screenW, 1, 0xFF1F2D45);

        // Header Title
        const char* hdr = "Morphic App Launcher";
        int hdrW = Compositor::MeasureText(hdr, 2);
        Compositor::DrawText((screenW - hdrW) / 2, 54, hdr, 0xFFF1F5F9, 2);

        // Glass Search Pill
        int searchW = 380;
        int searchH = 40;
        int searchX = (screenW - searchW) / 2;
        int searchY = 104;
        Compositor::DrawRoundedRect(searchX, searchY, searchW, searchH, 20, 0xFF141D30);
        Compositor::DrawRoundedBorder(searchX, searchY, searchW, searchH, 20, 0x4438BDF8);
        Compositor::DrawCircle(searchX + 20, searchY + 20, 5, 0xFF38BDF8);
        if (searchLen > 0) {
            Compositor::DrawText(searchX + 36, searchY + 12, searchQuery, 0xFFF1F5F9, 1);
            int curX = searchX + 36 + Compositor::MeasureText(searchQuery, 1) + 2;
            Compositor::DrawRect(curX, searchY + 11, 2, 18, 0xFF38BDF8);
        } else {
            Compositor::DrawText(searchX + 36, searchY + 12, "Type to filter apps (Enter to launch)...", 0xFF94A3B8, 1);
        }

        // Count visible filtered apps
        int visIndices[20];
        int visCount = 0;
        for (int i = 0; i < appCount; i++) {
            apps[i].w = 0;
            apps[i].h = 0;
            if (MatchesQuery(apps[i].name)) {
                visIndices[visCount++] = i;
            }
        }

        // App Grid (up to 5 columns, centered)
        int cols = visCount < 5 ? (visCount > 0 ? visCount : 1) : 5;
        int cardW = 116;
        int cardH = 124;
        int iconSize = 68;
        int gapX = 28;
        int gapY = 28;

        int totalRowW = (cols * cardW) + ((cols - 1) * gapX);
        int gridStartX = (screenW - totalRowW) / 2;
        int gridStartY = 184;

        for (int vi = 0; vi < visCount; vi++) {
            int i = visIndices[vi];
            int col = vi % 5;
            int row = vi / 5;
            int cx = gridStartX + col * (cardW + gapX);
            int cy = gridStartY + row * (cardH + gapY);

            // Glass tile card
            Compositor::DrawSoftShadow(cx, cy, cardW, cardH, 16, 10, 75);
            Compositor::DrawRoundedRect(cx, cy, cardW, cardH, 16, 0xEA131C2F);
            Compositor::DrawRoundedBorder(cx, cy, cardW, cardH, 16, 0x2EFFFFFF);

            // Gradient Squircle Icon
            int ix = cx + (cardW - iconSize) / 2;
            int iy = cy + 14;
            Compositor::DrawRoundedGradientV(ix, iy, iconSize, iconSize, 18, apps[i].colorTop, apps[i].colorBot);
            Compositor::DrawRoundedBorder(ix, iy, iconSize, iconSize, 18, 0x44FFFFFF);

            // Crisp 2-letter monogram
            char mono[3] = {apps[i].name[0], apps[i].name[1] ? apps[i].name[1] : '\0', '\0'};
            int mw = Compositor::MeasureText(mono, 2);
            Compositor::DrawText(ix + (iconSize - mw) / 2, iy + (iconSize - 32) / 2, mono, 0xFFFFFFFF, 2);

            // Hitbox covers entire card
            apps[i].x = cx;
            apps[i].y = cy;
            apps[i].w = cardW;
            apps[i].h = cardH;

            // Centered App Label
            int textW = Compositor::MeasureText(apps[i].name, 1);
            int textX = cx + (cardW - textW) / 2;
            int textY = iy + iconSize + 12;
            Compositor::DrawText(textX, textY, apps[i].name, 0xFFE2E8F0, 1);
        }

        // Bottom Power Controls Pill Bar
        int btnW = 104;
        int btnH = 34;
        int btnY = screenH - 104;
        int btnGap = 14;

        int shutdownX = screenW - btnW - 28;
        Compositor::DrawRoundedGradientV(shutdownX, btnY, btnW, btnH, 10, 0xFFFB7185, 0xFFE11D48);
        Compositor::DrawRoundedBorder(shutdownX, btnY, btnW, btnH, 10, 0x44FFFFFF);
        Compositor::DrawText(shutdownX + 28, btnY + 9, "Apagar", 0xFFFFFFFF, 1);

        int rebootX = shutdownX - btnW - btnGap;
        Compositor::DrawRoundedGradientV(rebootX, btnY, btnW, btnH, 10, 0xFF38BDF8, 0xFF0284C7);
        Compositor::DrawRoundedBorder(rebootX, btnY, btnW, btnH, 10, 0x44FFFFFF);
        Compositor::DrawText(rebootX + 16, btnY + 9, "Reiniciar", 0xFF06101C, 1);
    }

    bool HandleClick(int mx, int my) {
        int screenW = Compositor::GetWidth();
        int screenH = Compositor::GetHeight();

        int btnW = 104;
        int btnH = 34;
        int btnY = screenH - 104;
        int btnGap = 14;

        int shutdownX = screenW - btnW - 28;
        int rebootX = shutdownX - btnW - btnGap;

        if (mx >= shutdownX && mx < shutdownX + btnW && my >= btnY && my < btnY + btnH) {
            sys_shutdown();
            return true;
        }
        if (mx >= rebootX && mx < rebootX + btnW && my >= btnY && my < btnY + btnH) {
            sys_reboot();
            return true;
        }

        for (int i = 0; i < appCount; i++) {
            if (apps[i].w <= 0 || apps[i].h <= 0) continue;
            if (mx >= apps[i].x && mx < apps[i].x + apps[i].w &&
                my >= apps[i].y && my < apps[i].y + apps[i].h) {
                if (apps[i].mpk_path[0] && !spawnPending) {
                    spawnPending = true;
                    sys_spawn(apps[i].mpk_path);
                    return true;
                }
            }
        }
        return false;
    }

private:
    AppItem apps[20];
    int appCount;
    char searchQuery[32];
    int searchLen;

    static char ToLower(char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }

    bool MatchesQuery(const char* name) const {
        if (searchLen == 0) return true;
        for (int i = 0; name[i]; i++) {
            int j = 0;
            while (j < searchLen && name[i + j] && ToLower(name[i + j]) == ToLower(searchQuery[j])) {
                j++;
            }
            if (j == searchLen) return true;
        }
        return false;
    }

    void ScanDirectory(const char* dirPath) {
        DirEntry entries[32];
        int count = sys_readdir(dirPath, entries, 32);
        if (count <= 0) return;

        char base[64];
        int p = 0;
        for (int i = 0; dirPath[i] && p < 61; i++) base[p++] = dirPath[i];
        if (p > 0 && base[p - 1] != '/') base[p++] = '/';
        base[p] = '\0';

        for (int i = 0; i < count && appCount < 20; i++) {
            if (entries[i].type != 0) continue;
            if (StrCmp(entries[i].name, ".") == 0 || StrCmp(entries[i].name, "..") == 0) continue;
            if (!EndsWith(entries[i].name, ".mpk")) continue;
            if (StrCmp(entries[i].name, "compositor.mpk") == 0) continue;
            if (StrCmp(entries[i].name, "display_probe.mpk") == 0) continue;
            if (AlreadyListed(entries[i].name)) continue;
            BuildAppItem(entries[i].name, base);
        }
    }

    bool AlreadyListed(const char* filename) {
        char candidate[32];
        int i = 0;
        while (filename[i] && filename[i] != '.' && i < 31) {
            candidate[i] = filename[i];
            i++;
        }
        candidate[i] = '\0';
        if (candidate[0] >= 'a' && candidate[0] <= 'z') candidate[0] = (char)(candidate[0] - 'a' + 'A');
        for (int k = 0; k < appCount; k++) {
            if (StrCmp(apps[k].name, candidate) == 0) return true;
        }
        return false;
    }

    int StrLen(const char* s) { int l = 0; while (s[l]) l++; return l; }

    bool EndsWith(const char* str, const char* suffix) {
        int strLen = StrLen(str), sufLen = StrLen(suffix);
        if (sufLen > strLen) return false;
        for (int i = 0; i < sufLen; i++) {
            if (str[strLen - sufLen + i] != suffix[i]) return false;
        }
        return true;
    }

    int StrCmp(const char* a, const char* b) {
        while (*a && *b && *a == *b) { a++; b++; }
        return (unsigned char)*a - (unsigned char)*b;
    }

    void BuildAppItem(const char* filename, const char* basePath) {
        AppItem& app = apps[appCount];
        int i = 0;
        const char* p = basePath;
        while (*p && i < 63) app.mpk_path[i++] = *p++;
        p = filename;
        while (*p && i < 63) app.mpk_path[i++] = *p++;
        app.mpk_path[i] = '\0';

        i = 0;
        while (filename[i] && filename[i] != '.' && i < 31) {
            app.name[i] = filename[i];
            i++;
        }
        app.name[i] = '\0';
        if (app.name[0] >= 'a' && app.name[0] <= 'z') {
            app.name[0] = (char)(app.name[0] - 'a' + 'A');
        }

        AssignGradient(app.name, app.colorTop, app.colorBot);
        app.x = app.y = app.w = app.h = 0;
        appCount++;
    }

    void AssignGradient(const char* name, uint32_t& top, uint32_t& bot) {
        if (name[0] == 'C') { top = 0xFF34D399; bot = 0xFF059669; }       // Calculator: Emerald
        else if (name[0] == 'T') { top = 0xFF818CF8; bot = 0xFF4F46E5; }  // Terminal: Indigo
        else if (name[0] == 'F') { top = 0xFFFBBF24; bot = 0xFFD97706; }  // Filemanager: Amber
        else if (name[0] == 'I') { top = 0xFFF472B6; bot = 0xFFDB2777; }  // Installer: Rose/Pink
        else if (name[0] == 'D') { top = 0xFF38BDF8; bot = 0xFF0284C7; }  // Desktop: Cyan
        else { top = 0xFF2DD4BF; bot = 0xFF0D9488; }                      // Custom MPK: Teal
    }
};
