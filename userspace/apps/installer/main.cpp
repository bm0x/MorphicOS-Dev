#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"

namespace {

static void AppendStr(char* dst, int& pos, int maxLen, const char* s) {
    while (*s && pos < maxLen - 1) dst[pos++] = *s++;
    dst[pos] = '\0';
}

static void AppendU64(char* dst, int& pos, int maxLen, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0 && pos < maxLen - 1) dst[pos++] = tmp[--n];
    dst[pos] = '\0';
}

static bool EndsWithMpk(const char* name) {
    size_t len = MorphicAPI::Helpers::strlen(name);
    if (len < 4) return false;
    return name[len - 4] == '.' && name[len - 3] == 'm' && name[len - 2] == 'p' && name[len - 1] == 'k';
}

struct DiscoveredPkg {
    char name[48];
    char path[96];
    char source[24];
    uint32_t size;
    bool installed;
    bool validHeader;
    MorphicHW::MPKPackageInfo info;
};

class MPKInstallerApp : public MorphicAPI::Window {
public:
    MPKInstallerApp()
        : MorphicAPI::Window(680, 440),
          pkgCount(0),
          selectedIdx(0),
          progressPct(0) {
        MorphicAPI::Helpers::strcpy_s(statusMsg, "Ready. Select a .mpk package to inspect, install, or launch.", sizeof(statusMsg));
        statusColor = MorphicUI::Theme::TEXT_SECONDARY;
    }

    bool Init() override {
        if (!MorphicAPI::Window::Init()) return false;
        ScanPackages();
        Invalidate();
        return true;
    }

    void OnUpdate() override {}

    void OnRender(MorphicAPI::Graphics& g) override {
        using namespace MorphicUI;
        DrawWindowBackdrop(g);
        DrawHeaderBar(g, "Morphic Package Installer", "Inspect, validate, and install .mpk applications", 54);

        // Badge showing total packages
        char countBadge[32];
        int p = 0;
        AppendU64(countBadge, p, 32, (uint64_t)pkgCount);
        AppendStr(countBadge, p, 32, " PKGS");
        DrawBadge(g, (int)width - 96, 16, countBadge, Theme::ACCENT_CYAN, Theme::ACCENT_GLOW);

        // Left Pane: Package List Card
        Rect listCard = {16, 68, 300, 294};
        DrawCard(g, listCard, Theme::RADIUS_MD, Theme::SURFACE_CARD);
        g.DrawText(listCard.x + 14, listCard.y + 12, "AVAILABLE PACKAGES", Theme::TEXT_MUTED, 1);

        int rowY = listCard.y + 36;
        int maxVisible = 7;
        for (int i = 0; i < pkgCount && i < maxVisible; i++) {
            Rect row = {listCard.x + 10, rowY, listCard.w - 20, 32};
            bool isSel = (i == selectedIdx);
            bool isHover = row.Contains(GetMouseX(), GetMouseY());
            uint32_t rowBg = isSel ? 0xFF1F314D : (isHover ? Theme::SURFACE_HOVER : 0xFF121A2B);
            uint32_t rowBorder = isSel ? Theme::BORDER_FOCUS : Theme::BORDER_GLASS;

            g.FillRoundedRect(row.x, row.y, row.w, row.h, 7, rowBg);
            g.DrawRoundedRect(row.x, row.y, row.w, row.h, 7, rowBorder);

            g.DrawText(row.x + 10, row.y + 8, packages[i].name,
                       isSel ? Theme::TEXT_PRIMARY : Theme::TEXT_SECONDARY, 1);

            if (packages[i].installed) {
                g.FillCircle(row.x + row.w - 14, row.y + 16, 4, Theme::SUCCESS);
            }
            rowY += 36;
        }

        if (pkgCount == 0) {
            g.DrawText(listCard.x + 16, listCard.y + 48, "No .mpk files found.", Theme::TEXT_MUTED, 1);
        }

        // Right Pane: Inspector & Actions Card
        Rect detailCard = {330, 68, (int)width - 346, 294};
        DrawCard(g, detailCard, Theme::RADIUS_MD, Theme::SURFACE_CARD);

        if (pkgCount > 0 && selectedIdx >= 0 && selectedIdx < pkgCount) {
            const DiscoveredPkg& pkg = packages[selectedIdx];
            int dx = detailCard.x + 16;
            int dy = detailCard.y + 14;
            int dw = detailCard.w - 32;

            g.DrawText(dx, dy, pkg.name, Theme::TEXT_PRIMARY, 1);
            if (pkg.validHeader) {
                DrawBadge(g, detailCard.x + detailCard.w - 94, dy - 3, "MPK1 OK", Theme::SUCCESS, Theme::SUCCESS_BG);
            } else {
                DrawBadge(g, detailCard.x + detailCard.w - 96, dy - 3, "INVALID", Theme::DANGER, Theme::DANGER_BG);
            }

            dy += 28;
            g.DrawHLine(dx, dy, dw, Theme::DIVIDER);
            dy += 12;

            DrawKeyValue(g, dx, dy, dw, "Source:", pkg.source, Theme::TEXT_PRIMARY);
            dy += 22;

            char val[48];
            p = 0;
            AppendU64(val, p, 48, pkg.size / 1024);
            AppendStr(val, p, 48, " KB");
            DrawKeyValue(g, dx, dy, dw, "Package Size:", val, Theme::ACCENT_CYAN);
            dy += 22;

            p = 0;
            AppendU64(val, p, 48, pkg.info.code_size / 1024);
            AppendStr(val, p, 48, " KB");
            DrawKeyValue(g, dx, dy, dw, "Code Segment:", val, Theme::TEXT_PRIMARY);
            dy += 22;

            p = 0;
            AppendU64(val, p, 48, pkg.info.assets_size / 1024);
            AppendStr(val, p, 48, " KB");
            DrawKeyValue(g, dx, dy, dw, "Assets Blob:", val, Theme::TEXT_PRIMARY);
            dy += 22;

            DrawKeyValue(g, dx, dy, dw, "Target:", "/data/apps", Theme::TEXT_SECONDARY);
            dy += 28;

            DrawProgressBar(g, {dx, dy, dw, 10}, progressPct);
            dy += 24;

            // Action Buttons
            btnInstall = {dx, dy, (dw - 10) / 2, 34};
            btnLaunch  = {dx + (dw - 10) / 2 + 10, dy, (dw - 10) / 2, 34};
            btnRefresh = {dx, dy + 42, dw, 30};

            DrawButton(g, btnInstall, pkg.installed ? "Reinstall" : "Install",
                       ButtonStyle::PRIMARY, btnInstall.Contains(GetMouseX(), GetMouseY()), !pkg.validHeader);
            DrawButton(g, btnLaunch, "Launch App",
                       ButtonStyle::SUCCESS, btnLaunch.Contains(GetMouseX(), GetMouseY()), !pkg.validHeader);
            DrawButton(g, btnRefresh, "Rescan Storage",
                       ButtonStyle::SECONDARY, btnRefresh.Contains(GetMouseX(), GetMouseY()));
        }

        // Bottom Status Strip
        Rect statusBar = {16, 374, (int)width - 32, 48};
        DrawCard(g, statusBar, Theme::RADIUS_SM, 0xFF101828);
        g.DrawText(statusBar.x + 14, statusBar.y + 16, statusMsg, statusColor, 1);
    }

    void OnMouseMove(int x, int y) override {
        (void)x; (void)y;
        Invalidate();
    }

    void OnMouseDown(int x, int y, int btn) override {
        (void)btn;
        // Package list click
        int rowY = 68 + 36;
        for (int i = 0; i < pkgCount && i < 7; i++) {
            MorphicUI::Rect row = {26, rowY, 280, 32};
            if (row.Contains(x, y)) {
                selectedIdx = i;
                progressPct = packages[i].installed ? 100 : 0;
                InspectSelected();
                Invalidate();
                return;
            }
            rowY += 36;
        }

        if (pkgCount > 0 && selectedIdx >= 0 && selectedIdx < pkgCount) {
            if (btnInstall.Contains(x, y) && packages[selectedIdx].validHeader) {
                InstallSelected();
                Invalidate();
                return;
            }
            if (btnLaunch.Contains(x, y) && packages[selectedIdx].validHeader) {
                int pid = MorphicHW::Package::Launch(packages[selectedIdx].path);
                if (pid > 0) {
                    MorphicAPI::Helpers::strcpy_s(statusMsg, "Launched application process successfully.", sizeof(statusMsg));
                    statusColor = MorphicUI::Theme::SUCCESS;
                } else {
                    MorphicAPI::Helpers::strcpy_s(statusMsg, "Failed to spawn application package.", sizeof(statusMsg));
                    statusColor = MorphicUI::Theme::DANGER;
                }
                Invalidate();
                return;
            }
            if (btnRefresh.Contains(x, y)) {
                ScanPackages();
                MorphicAPI::Helpers::strcpy_s(statusMsg, "Rescanned /initrd, /data, and /data/apps.", sizeof(statusMsg));
                statusColor = MorphicUI::Theme::ACCENT_CYAN;
                Invalidate();
                return;
            }
        }
    }

private:
    DiscoveredPkg packages[16];
    int pkgCount;
    int selectedIdx;
    int progressPct;
    char statusMsg[128];
    uint32_t statusColor;

    MorphicUI::Rect btnInstall;
    MorphicUI::Rect btnLaunch;
    MorphicUI::Rect btnRefresh;

    void ScanDir(const char* dir, const char* label, bool markInstalled) {
        DirEntry entries[32];
        int n = MorphicHW::Storage::ListDir(dir, entries, 32);
        if (n <= 0) return;

        for (int i = 0; i < n && pkgCount < 16; i++) {
            if (entries[i].type != 0) continue;
            if (!EndsWithMpk(entries[i].name)) continue;
            if (MorphicAPI::Helpers::strcmp(entries[i].name, "compositor.mpk") == 0) continue;

            // Check if already discovered (mark installed if found in /data/apps)
            bool dup = false;
            for (int k = 0; k < pkgCount; k++) {
                if (MorphicAPI::Helpers::strcmp(packages[k].name, entries[i].name) == 0) {
                    if (markInstalled) packages[k].installed = true;
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            DiscoveredPkg& pkg = packages[pkgCount];
            MorphicAPI::Helpers::memset(&pkg, 0, sizeof(pkg));
            MorphicAPI::Helpers::strcpy_s(pkg.name, entries[i].name, sizeof(pkg.name));
            MorphicAPI::Helpers::strcpy_s(pkg.source, label, sizeof(pkg.source));
            int p = 0;
            AppendStr(pkg.path, p, sizeof(pkg.path), dir);
            AppendStr(pkg.path, p, sizeof(pkg.path), "/");
            AppendStr(pkg.path, p, sizeof(pkg.path), entries[i].name);
            pkg.size = entries[i].size;
            pkg.installed = markInstalled;
            pkgCount++;
        }
    }

    void InspectSelected() {
        if (pkgCount == 0 || selectedIdx < 0 || selectedIdx >= pkgCount) return;
        DiscoveredPkg& pkg = packages[selectedIdx];
        uint8_t hdr[64];
        int rd = MorphicHW::Storage::ReadFile(pkg.path, hdr, 64);
        if (rd >= 64) {
            // Use reported size for bounds validation
            uint64_t totalSz = pkg.size >= 64 ? pkg.size : (uint64_t)rd;
            pkg.validHeader = MorphicHW::Package::ValidateHeader(hdr, totalSz, &pkg.info);
        } else {
            pkg.validHeader = false;
        }
    }

    void ScanPackages() {
        pkgCount = 0;
        ScanDir("/initrd", "InitRD", false);
        ScanDir("/data", "Data Root", false);
        ScanDir("/data/apps", "Installed", true);
        if (selectedIdx >= pkgCount) selectedIdx = 0;
        for (int i = 0; i < pkgCount; i++) {
            int saved = selectedIdx;
            selectedIdx = i;
            InspectSelected();
            selectedIdx = saved;
        }
        if (pkgCount > 0) {
            progressPct = packages[selectedIdx].installed ? 100 : 0;
        }
    }

    void InstallSelected() {
        DiscoveredPkg& pkg = packages[selectedIdx];
        progressPct = 35;
        int res = MorphicHW::Package::Install(pkg.path, pkg.name, "/data/apps");
        if (res > 0) {
            pkg.installed = true;
            progressPct = 100;
            int p = 0;
            AppendStr(statusMsg, p, sizeof(statusMsg), "Installed ");
            AppendStr(statusMsg, p, sizeof(statusMsg), pkg.name);
            AppendStr(statusMsg, p, sizeof(statusMsg), " -> /data/apps (");
            AppendU64(statusMsg, p, sizeof(statusMsg), (uint64_t)(res / 1024));
            AppendStr(statusMsg, p, sizeof(statusMsg), " KB)");
            statusColor = MorphicUI::Theme::SUCCESS;
        } else {
            progressPct = 0;
            int p = 0;
            AppendStr(statusMsg, p, sizeof(statusMsg), "Install error (code ");
            AppendU64(statusMsg, p, sizeof(statusMsg), (uint64_t)(-res));
            AppendStr(statusMsg, p, sizeof(statusMsg), "). Mount a writable FAT32 /data disk.");
            statusColor = MorphicUI::Theme::WARNING;
        }
    }
};

} // namespace

extern "C" int main(void* args) {
    (void)args;
    MPKInstallerApp app;
    app.Run();
    return 0;
}
