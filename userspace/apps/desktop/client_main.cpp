#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"

namespace {

static void AppendText(char* out, int& pos, int max_len, const char* text) {
    while (*text && pos < max_len - 1) out[pos++] = *text++;
    out[pos] = '\0';
}

static void AppendU64(char* out, int& pos, int max_len, uint64_t value) {
    char tmp[24];
    int count = 0;
    if (value == 0) tmp[count++] = '0';
    while (value && count < 23) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && pos < max_len - 1) out[pos++] = tmp[--count];
    out[pos] = '\0';
}

class DesktopClientApp : public MorphicAPI::Window {
public:
    DesktopClientApp()
        : MorphicAPI::Window(560, 310),
          currentPid(0),
          compositorPid(0),
          lastRefreshMs(0),
          linked(false),
          statusDirty(true) {}

    bool Init() override {
        if (!MorphicAPI::Window::Init()) return false;
        RefreshStatus();
        Invalidate();
        return true;
    }

    void OnUpdate() override {
        uint64_t now = sys_get_time_ms();
        if (lastRefreshMs == 0 || (now - lastRefreshMs) >= 500) {
            RefreshStatus();
            lastRefreshMs = now;
        }
        if (statusDirty) {
            Invalidate();
            statusDirty = false;
        }
    }

    void OnMouseMove(int x, int y) override {
        (void)x; (void)y;
        Invalidate();
    }

    void OnRender(MorphicAPI::Graphics& g) override {
        using namespace MorphicUI;
        DrawWindowBackdrop(g);
        DrawHeaderBar(g, "Morphic Desktop Hub", "MorphicAPI UI v2  |  Glass Dark Runtime", 54);

        DrawBadge(g, (int)width - 102, 16,
                  linked ? "LINKED" : "WAIT",
                  linked ? Theme::SUCCESS : Theme::WARNING,
                  linked ? Theme::SUCCESS_BG : Theme::WARNING_BG);

        // Card 1: Compositor & IPC Telemetry
        Rect infoCard = {18, 68, (int)width - 36, 134};
        DrawCard(g, infoCard);

        int x = infoCard.x + 16;
        int w = infoCard.w - 32;
        int y = infoCard.y + 12;

        g.DrawText(x, y, "COMPOSITOR PROTOCOL", Theme::ACCENT_CYAN, 1);
        y += 24;

        char buf[64];
        int pos = 0;
        AppendText(buf, pos, 64, "PID ");
        AppendU64(buf, pos, 64, currentPid);
        AppendText(buf, pos, 64, " -> WM ");
        AppendU64(buf, pos, 64, compositorPid);
        DrawKeyValue(g, x, y, w, "Session Link", buf, Theme::TEXT_PRIMARY);
        y += 22;

        pos = 0;
        AppendU64(buf, pos, 64, GetProtocolSubmitSerial());
        AppendText(buf, pos, 64, " / ");
        AppendU64(buf, pos, 64, GetProtocolAckSerial());
        DrawKeyValue(g, x, y, w, "Frames Submit / Ack", buf, Theme::SUCCESS);
        y += 22;

        pos = 0;
        AppendU64(buf, pos, 64, GetProtocolTimeoutRecoveries());
        AppendText(buf, pos, 64, " timeouts  |  ");
        AppendU64(buf, pos, 64, GetProtocolThrottleSleeps());
        AppendText(buf, pos, 64, " waits");
        DrawKeyValue(g, x, y, w, "Sync Health", buf, Theme::TEXT_SECONDARY);
        y += 22;

        DrawKeyValue(g, x, y, w, "Protocol Version", "v1 Atomic IPC", Theme::ACCENT_CYAN);

        // Card 2: Quick App Launch Strip
        Rect launchCard = {18, 214, (int)width - 36, 78};
        DrawCard(g, launchCard);
        g.DrawText(launchCard.x + 16, launchCard.y + 10, "QUICK LAUNCH (.MPK APPS)", Theme::TEXT_MUTED, 1);

        int bw = (launchCard.w - 32 - 30) / 4;
        int by = launchCard.y + 34;
        int bx = launchCard.x + 16;
        btnInstaller = {bx, by, bw, 32};
        btnTerminal  = {bx + (bw + 10), by, bw, 32};
        btnCalc      = {bx + (bw + 10) * 2, by, bw, 32};
        btnFiles     = {bx + (bw + 10) * 3, by, bw, 32};

        char hzLbl[20];
        int hp = 0;
        AppendU64(hzLbl, hp, 20, refreshHz);
        AppendText(hzLbl, hp, 20, " Hz");
        hzLbl[hp] = '\0';

        DrawButton(g, btnInstaller, "Installer", ButtonStyle::PRIMARY, btnInstaller.Contains(mouseX, mouseY));
        DrawButton(g, btnTerminal,  "Terminal",  ButtonStyle::SECONDARY, btnTerminal.Contains(mouseX, mouseY));
        DrawButton(g, btnCalc,      "Calc",      ButtonStyle::SECONDARY, btnCalc.Contains(mouseX, mouseY));
        DrawButton(g, btnFiles,     hzLbl,       ButtonStyle::SUCCESS,   btnFiles.Contains(mouseX, mouseY));
    }

    void OnMouseDown(int x, int y, int btn) override {
        (void)btn;
        if (btnInstaller.Contains(x, y)) { MorphicHW::Package::Launch("/initrd/installer.mpk"); return; }
        if (btnTerminal.Contains(x, y))  { MorphicHW::Package::Launch("/initrd/terminal.mpk"); return; }
        if (btnCalc.Contains(x, y))      { MorphicHW::Package::Launch("/initrd/calculator.mpk"); return; }
        if (btnFiles.Contains(x, y)) {
            static const uint32_t kRates[] = {60, 75, 120, 144, 165, 240};
            uint32_t nextHz = 60;
            for (int i = 0; i < 6; i++) {
                if (kRates[i] == refreshHz) { nextHz = kRates[(i + 1) % 6]; break; }
            }
            if (MorphicHW::Display::SetRefreshRate(nextHz, GRAPHICS_VSYNC_ADAPTIVE)) {
                refreshHz = nextHz;
                Invalidate();
            }
            return;
        }
    }

    void OnKeyDown(char c) override {
        if (c == 'r' || c == 'R') {
            RefreshStatus();
            statusDirty = true;
        }
        MorphicAPI::Window::OnKeyDown(c);
    }

private:
    uint64_t currentPid;
    uint64_t compositorPid;
    uint64_t lastRefreshMs;
    bool linked;
    bool statusDirty;
    uint32_t refreshHz = 60;

    MorphicUI::Rect btnInstaller, btnTerminal, btnCalc, btnFiles;

    void RefreshStatus() {
        currentPid = sys_get_pid();
        compositorPid = sys_get_compositor_pid();
        linked = (currentPid != 0 && compositorPid != 0 && currentPid != compositorPid);
        refreshHz = MorphicHW::Display::GetRefreshRate();
        statusDirty = true;
    }
};

} // namespace

extern "C" int main(void* args) {
    (void)args;
    DesktopClientApp app;
    app.Run();
    return 0;
}
