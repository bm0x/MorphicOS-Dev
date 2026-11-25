#include "terminal.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"
#include "../../sdk/lua/morphic_lua.h"

static bool s_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool s_starts(const char* s, const char* p) {
    while (*p) { if (*s++ != *p++) return false; }
    return true;
}

TerminalApp::TerminalApp() : MorphicAPI::Window(720, 440) {
    MorphicAPI::Helpers::strcpy_s(cwd, "/initrd", sizeof(cwd));
    activeColor = MorphicUI::Theme::TEXT_PRIMARY;
    ClearScreen();

    activeColor = MorphicUI::Theme::ACCENT_CYAN;
    PrintLine("Morphic OS Terminal v2.0 (Ring-3 Interactive Shell)");
    activeColor = MorphicUI::Theme::TEXT_SECONDARY;
    PrintLine("Commands: help, ls, cd, pwd, cat, mem, gpu, net, mounts, hz, run, lua");
    activeColor = MorphicUI::Theme::TEXT_PRIMARY;
    PrintLine("");
    Prompt();
}

void TerminalApp::ClearScreen() {
    for (int i = 0; i < ROWS; i++) {
        MorphicAPI::Helpers::memset(lines[i], 0, COLS + 1);
        lineColors[i] = MorphicUI::Theme::TEXT_PRIMARY;
    }
    currentLine = 0;
    currentCol = 0;
    inputLen = 0;
    MorphicAPI::Helpers::memset(inputBuffer, 0, sizeof(inputBuffer));
}

void TerminalApp::Prompt() {
    currentCol = 0;
    MorphicAPI::Helpers::memset(lines[currentLine], 0, COLS + 1);
    lineColors[currentLine] = MorphicUI::Theme::ACCENT_CYAN;

    const char* prefix = "morphic:";
    while (*prefix && currentCol < COLS - 4) lines[currentLine][currentCol++] = *prefix++;
    for (int i = 0; cwd[i] && currentCol < COLS - 3; i++) lines[currentLine][currentCol++] = cwd[i];
    lines[currentLine][currentCol++] = '$';
    lines[currentLine][currentCol++] = ' ';
    lines[currentLine][currentCol] = '\0';

    inputLen = 0;
    MorphicAPI::Helpers::memset(inputBuffer, 0, sizeof(inputBuffer));
}

void TerminalApp::Scroll() {
    for (int i = 0; i < ROWS - 1; i++) {
        MorphicAPI::Helpers::memcpy(lines[i], lines[i + 1], COLS + 1);
        lineColors[i] = lineColors[i + 1];
    }
    MorphicAPI::Helpers::memset(lines[ROWS - 1], 0, COLS + 1);
    lineColors[ROWS - 1] = activeColor;
    currentLine = ROWS - 1;
    currentCol = 0;
}

void TerminalApp::NewLine() {
    currentCol = 0;
    currentLine++;
    if (currentLine >= ROWS) Scroll();
    MorphicAPI::Helpers::memset(lines[currentLine], 0, COLS + 1);
    lineColors[currentLine] = activeColor;
}

void TerminalApp::PutChar(char c) {
    if (c == '\n' || c == '\r') { NewLine(); return; }
    if (c < 32 && c != '\t') return;
    if (c == '\t') {
        int sp = 4 - (currentCol % 4);
        for (int i = 0; i < sp && currentCol < COLS; i++) lines[currentLine][currentCol++] = ' ';
        return;
    }
    if (currentCol >= COLS) NewLine();
    lineColors[currentLine] = activeColor;
    lines[currentLine][currentCol++] = c;
    if (currentCol < COLS) lines[currentLine][currentCol] = '\0';
}

void TerminalApp::Print(const char* text) {
    if (!text) return;
    while (*text) PutChar(*text++);
}

void TerminalApp::PrintLine(const char* text) {
    Print(text);
    NewLine();
}

void TerminalApp::PrintU64(uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    char out[24];
    int p = 0;
    while (n > 0) out[p++] = tmp[--n];
    out[p] = '\0';
    Print(out);
}

void TerminalApp::BuildFullPath(const char* arg, char* out, int maxL) {
    if (!arg || !*arg) { MorphicAPI::Helpers::strcpy_s(out, cwd, (size_t)maxL); return; }
    if (arg[0] == '/') { MorphicAPI::Helpers::strcpy_s(out, arg, (size_t)maxL); return; }
    int p = 0;
    for (int i = 0; cwd[i] && p < maxL - 2; i++) out[p++] = cwd[i];
    if (p > 0 && out[p - 1] != '/') out[p++] = '/';
    for (int i = 0; arg[i] && p < maxL - 1; i++) out[p++] = arg[i];
    out[p] = '\0';
}

void TerminalApp::ExecuteCommand(const char* raw) {
    while (*raw == ' ') raw++;
    if (!*raw) return;

    activeColor = MorphicUI::Theme::TEXT_PRIMARY;

    if (s_eq(raw, "clear") || s_eq(raw, "cls")) {
        ClearScreen();
        return;
    }

    if (s_eq(raw, "help")) {
        activeColor = MorphicUI::Theme::ACCENT_CYAN;
        PrintLine("--- Morphic Shell v2.0 ---");
        activeColor = MorphicUI::Theme::TEXT_PRIMARY;
        PrintLine("  ls [path]        List directory files");
        PrintLine("  cd <path>        Change directory (/initrd, /data)");
        PrintLine("  pwd / cat <f>    Print working dir / show text file");
        PrintLine("  mkdir / rm <f>   Create folder / delete file");
        PrintLine("  mem / gpu / net  Hardware & Intel e1000 NIC status");
        PrintLine("  mounts / uname   Mounted filesystems / OS version");
        PrintLine("  hz <30..240>     Set live VSync refresh rate");
        PrintLine("  run <app.mpk>    Launch .mpk application");
        PrintLine("  lua <stmt>       Run MorphicLua expression inline");
        PrintLine("  clear / reboot / shutdown");
        return;
    }

    if (s_eq(raw, "pwd")) {
        PrintLine(cwd);
        return;
    }

    if (s_eq(raw, "uname")) {
        activeColor = MorphicUI::Theme::SUCCESS;
        PrintLine("Morphic OS x86_64 | Glass Dark UI v2 | MorphicVK 1.3 | AHCI/NVMe/xHCI/e1000");
        return;
    }

    if (s_starts(raw, "echo ")) {
        PrintLine(raw + 5);
        return;
    }

    if (s_eq(raw, "cd") || s_starts(raw, "cd ")) {
        const char* arg = raw[2] ? raw + 3 : "/initrd";
        while (*arg == ' ') arg++;
        char target[96];
        BuildFullPath(arg, target, sizeof(target));
        DirEntry probe[2];
        if (MorphicHW::Storage::ListDir(target, probe, 2) >= 0) {
            MorphicAPI::Helpers::strcpy_s(cwd, target, sizeof(cwd));
        } else {
            activeColor = MorphicUI::Theme::DANGER;
            Print("cd: directory not found: ");
            PrintLine(target);
        }
        return;
    }

    if (s_eq(raw, "ls") || s_starts(raw, "ls ")) {
        const char* arg = raw[2] ? raw + 3 : "";
        while (*arg == ' ') arg++;
        char target[96];
        BuildFullPath(arg, target, sizeof(target));
        DirEntry entries[32];
        int count = MorphicHW::Storage::ListDir(target, entries, 32);
        if (count < 0) {
            activeColor = MorphicUI::Theme::DANGER;
            Print("ls: cannot access "); PrintLine(target);
            return;
        }
        for (int i = 0; i < count; i++) {
            activeColor = entries[i].type == 1 ? MorphicUI::Theme::WARNING : MorphicUI::Theme::TEXT_PRIMARY;
            Print(entries[i].type == 1 ? "[DIR]  " : "[FILE] ");
            Print(entries[i].name);
            if (entries[i].type == 0) {
                Print("  ("); PrintU64(entries[i].size); Print(" B)");
            }
            NewLine();
        }
        return;
    }

    if (s_starts(raw, "cat ")) {
        const char* arg = raw + 4;
        while (*arg == ' ') arg++;
        char target[96];
        BuildFullPath(arg, target, sizeof(target));
        char buf[513];
        int n = MorphicHW::Storage::ReadFile(target, buf, 512);
        if (n < 0) {
            activeColor = MorphicUI::Theme::DANGER;
            Print("cat: failed to read "); PrintLine(target);
        } else {
            buf[n] = '\0';
            PrintLine(buf);
        }
        return;
    }

    if (s_starts(raw, "mkdir ")) {
        char target[96]; BuildFullPath(raw + 6, target, sizeof(target));
        int r = MorphicHW::Storage::CreateDir(target);
        activeColor = (r >= 0) ? MorphicUI::Theme::SUCCESS : MorphicUI::Theme::DANGER;
        PrintLine(r >= 0 ? "Directory created." : "mkdir failed (check writable mount).");
        return;
    }

    if (s_starts(raw, "rm ")) {
        char target[96]; BuildFullPath(raw + 3, target, sizeof(target));
        int r = MorphicHW::Storage::DeleteFile(target);
        activeColor = (r >= 0) ? MorphicUI::Theme::SUCCESS : MorphicUI::Theme::DANGER;
        PrintLine(r >= 0 ? "File deleted." : "rm failed.");
        return;
    }

    if (s_eq(raw, "mem")) {
        MorphicSystemInfo si = {};
        if (MorphicHW::System::GetInfo(&si)) {
            Print("CPU: "); PrintLine(si.cpu_vendor);
            Print("RAM Total: "); PrintU64(si.total_mem_bytes / (1024ULL * 1024ULL));
            Print(" MB | Free: "); PrintU64(si.free_mem_bytes / (1024ULL * 1024ULL)); PrintLine(" MB");
            Print("Uptime: "); PrintU64(MorphicHW::System::GetUptimeMs()); PrintLine(" ms");
        }
        return;
    }

    if (s_eq(raw, "gpu")) {
        MorphicVkPhysicalDeviceInfo gpu = {};
        activeColor = MorphicUI::Theme::SUCCESS;
        if (MorphicHW::GPU::QueryDevice(&gpu)) {
            Print("GPU: "); Print(gpu.vendor_name); Print(" | "); PrintLine(gpu.device_name);
        } else {
            PrintLine("GPU: Software Framebuffer");
        }
        activeColor = MorphicUI::Theme::ACCENT_CYAN;
        Print("VSync Target: "); PrintU64(MorphicHW::Display::GetRefreshRate());
        Print(" Hz | Measured: "); PrintU64(MorphicHW::Display::GetMeasuredFps()); PrintLine(" FPS");
        return;
    }

    if (s_eq(raw, "net")) {
        MorphicNetInfo ni = {};
        if (MorphicHW::Network::GetInfo(&ni)) {
            activeColor = ni.link_up ? MorphicUI::Theme::SUCCESS : MorphicUI::Theme::WARNING;
            Print("NIC: "); Print(ni.driver_name); Print(ni.link_up ? " [LINK UP]" : " [LINK DOWN]"); NewLine();
            activeColor = MorphicUI::Theme::TEXT_PRIMARY;
            Print("MAC: ");
            const char* hx = "0123456789ABCDEF";
            char mbuf[18]; int p = 0;
            for (int m = 0; m < 6; m++) {
                mbuf[p++] = hx[(ni.mac[m] >> 4) & 0xF];
                mbuf[p++] = hx[ni.mac[m] & 0xF];
                if (m < 5) mbuf[p++] = ':';
            }
            mbuf[p] = '\0';
            PrintLine(mbuf);
            Print("TX: "); PrintU64(ni.tx_packets); Print(" pkts | RX: "); PrintU64(ni.rx_packets); PrintLine(" pkts");
        } else {
            activeColor = MorphicUI::Theme::WARNING;
            PrintLine("No Intel e1000 NIC detected on PCI bus.");
        }
        return;
    }

    if (s_eq(raw, "mounts")) {
        MountEntry m[8];
        int n = MorphicHW::Storage::ListMounts(m, 8);
        for (int i = 0; i < n; i++) {
            Print(m[i].path); Print("  ("); Print(m[i].fstype); PrintLine(")");
        }
        return;
    }

    if (s_starts(raw, "hz ")) {
        uint32_t hz = (uint32_t)MorphicLua::ParseInt64(raw + 3);
        if (MorphicHW::Display::SetRefreshRate(hz)) {
            activeColor = MorphicUI::Theme::SUCCESS;
            Print("Display VSync set to "); PrintU64(hz); PrintLine(" Hz.");
        } else {
            activeColor = MorphicUI::Theme::DANGER;
            PrintLine("Usage: hz <30..240> (e.g. hz 144)");
        }
        return;
    }

    if (s_starts(raw, "run ")) {
        char target[96]; BuildFullPath(raw + 4, target, sizeof(target));
        int pid = MorphicHW::Package::Launch(target);
        activeColor = (pid > 0) ? MorphicUI::Theme::SUCCESS : MorphicUI::Theme::DANGER;
        if (pid > 0) { Print("Spawned PID "); PrintU64((uint64_t)pid); NewLine(); }
        else PrintLine("Failed to launch .mpk");
        return;
    }

    if (s_starts(raw, "lua ")) {
        MorphicLua::EvalContext lctx = {};
        lctx.gfx = nullptr;
        lctx.user = this;
        lctx.print_cb = [](const char* msg, void* u) { ((TerminalApp*)u)->Print(msg); };
        MorphicLua::EvalLine(raw + 4, lctx);
        return;
    }

    if (s_eq(raw, "reboot")) { MorphicHW::System::Reboot(); return; }
    if (s_eq(raw, "shutdown")) { MorphicHW::System::Shutdown(); return; }

    // Fallback to legacy MCL parser for compatibility
    MCL::ProcessCommand(raw, this);
}

void TerminalApp::OnKeyDown(char c) {
    if (c == 0 || c == 27) return;

    if (c == '\n' || c == '\r') {
        NewLine();
        if (inputLen > 0) {
            ExecuteCommand(inputBuffer);
        }
        Prompt();
        Invalidate();
        return;
    }

    if (c == '\b' || c == 127) {
        if (inputLen > 0) {
            inputLen--;
            inputBuffer[inputLen] = '\0';
            if (currentCol > 0) {
                currentCol--;
                lines[currentLine][currentCol] = '\0';
            }
            Invalidate();
        }
        return;
    }

    if (c >= 32 && c <= 126 && inputLen < 126 && currentCol < COLS - 1) {
        inputBuffer[inputLen++] = c;
        inputBuffer[inputLen] = '\0';
        lines[currentLine][currentCol++] = c;
        lines[currentLine][currentCol] = '\0';
        Invalidate();
    }
}

void TerminalApp::OnRender(MorphicAPI::Graphics& g) {
    using namespace MorphicUI;
    DrawWindowBackdrop(g);
    DrawHeaderBar(g, "Morphic Terminal", "POSIX + MCL + MorphicLua Interactive Shell", 46);
    DrawBadge(g, (int)width - 124, 12, cwd, Theme::ACCENT_CYAN, Theme::ACCENT_GLOW);

    const int CHAR_HEIGHT = 18;
    const int PADDING_X = 14;
    const int PADDING_Y = 56;

    for (int row = 0; row < ROWS; row++) {
        int y = PADDING_Y + (row * CHAR_HEIGHT);
        if (y + 16 > (int)height - 8) break;
        if (lines[row][0] != '\0') {
            g.DrawText(PADDING_X, y, lines[row], lineColors[row], 1);
        }
    }

    int cursorX = PADDING_X + (currentCol * 9);
    int cursorY = PADDING_Y + (currentLine * CHAR_HEIGHT);
    if (cursorY + 16 <= (int)height - 8) {
        g.FillRoundedRect(cursorX, cursorY + 1, 8, 15, 2, Theme::SUCCESS);
    }
}

void TerminalApp::DrawChar(MorphicAPI::Graphics& g, int x, int y, char c, uint32_t color) {
    g.DrawChar(x, y, c, color, 1);
}
