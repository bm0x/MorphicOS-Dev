#include "../../sdk/morphic_api.h"

namespace {

static void AppendText(char* out, int& pos, int max_len, const char* text) {
    while (*text && pos < max_len - 1) {
        out[pos++] = *text++;
    }
}

static void AppendU64(char* out, int& pos, int max_len, uint64_t value) {
    char tmp[24];
    int count = 0;
    if (value == 0) {
        tmp[count++] = '0';
    }
    while (value && count < 23) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && pos < max_len - 1) {
        out[pos++] = tmp[--count];
    }
}

static uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

class DesktopClientApp : public MorphicAPI::Window {
public:
    DesktopClientApp()
        : MorphicAPI::Window(520, 220),
          currentPid(0),
          compositorPid(0),
                    lastRefreshMs(0),
                    linked(false),
          statusDirty(true) {}

    bool Init() override {
        if (!MorphicAPI::Window::Init()) {
            return false;
        }

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

    void OnRender(MorphicAPI::Graphics& g) override {
        g.Clear(MakeColor(18, 24, 34));
        g.FillRect(0, 0, (int)width, 40, MakeColor(32, 52, 74));
        g.DrawText(18, 12, "Desktop Client", COLOR_WHITE, 1);

        g.FillRect(18, 60, (int)width - 36, 118, MakeColor(28, 36, 48));
        g.DrawText(30, 74, linked ? "State: linked to compositor" : "State: waiting for compositor", linked ? 0xFF88CC88 : 0xFFCCAA66, 1);

        char line[96];
        int pos = 0;
        AppendText(line, pos, 96, "Client PID: ");
        AppendU64(line, pos, 96, currentPid);
        line[pos] = 0;
        g.DrawText(30, 98, line, 0xFFD8DDE6, 1);

        pos = 0;
        AppendText(line, pos, 96, "Compositor PID: ");
        AppendU64(line, pos, 96, compositorPid);
        line[pos] = 0;
        g.DrawText(30, 118, line, 0xFFD8DDE6, 1);

        pos = 0;
        AppendText(line, pos, 96, "Protocol: ");
        AppendU64(line, pos, 96, COMPOSITOR_PROTOCOL_VERSION);
        line[pos] = 0;
        g.DrawText(30, 138, line, 0xFFD8DDE6, 1);

        g.DrawText(30, 162, "Press R to refresh status", 0xFF9FB3C8, 1);
        g.DrawText(30, 182, "HELLO/CREATE/COMMIT via MorphicAPI Window", 0xFF9FB3C8, 1);
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

    void RefreshStatus() {
        currentPid = sys_get_pid();
        compositorPid = sys_get_compositor_pid();
        linked = (currentPid != 0 && compositorPid != 0 && currentPid != compositorPid);
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