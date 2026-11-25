#pragma once
#include "../../sdk/morphic_api.h"
#include "../../sdk/morphic_ui.h"
#include "../../sdk/morphic_hw.h"

// ============================================================================
// Morphic Browser v1.0 - Native HTML/UI Renderer + Edge WebView2 Bridge Client
// ============================================================================

class MorphicBrowserApp : public MorphicAPI::Window {
public:
    MorphicBrowserApp()
        : MorphicAPI::Window(780, 490),
          urlLen(0), urlFocused(false), activePage(0) {
        SetUrl("morphic://home");
    }

    bool Init() override {
        if (!MorphicAPI::Window::Init()) return false;
        MorphicHW::Network::GetInfo(&netInfo);
        MorphicHW::GPU::QueryDevice(&gpuInfo);
        Invalidate();
        return true;
    }

    void OnUpdate() override {}
    void OnMouseMove(int x, int y) override { (void)x; (void)y; Invalidate(); }

    void OnRender(MorphicAPI::Graphics& g) override {
        using namespace MorphicUI;
        DrawWindowBackdrop(g);

        // Top Browser Chrome / Toolbar
        g.FillGradientV(0, 0, (int)width, 54, 0xFF162036, 0xFF101829);
        g.DrawHLine(0, 53, (int)width, Theme::BORDER_GLASS);

        btnHome   = {14, 11, 66, 32};
        btnDocs   = {86, 11, 66, 32};
        btnNet    = {158, 11, 66, 32};
        urlBar    = {234, 11, (int)width - 336, 32};
        btnGo     = {(int)width - 92, 11, 78, 32};

        DrawButton(g, btnHome, "Home", activePage == 0 ? ButtonStyle::PRIMARY : ButtonStyle::SECONDARY, btnHome.Contains(mouseX, mouseY));
        DrawButton(g, btnDocs, "SDK",  activePage == 1 ? ButtonStyle::PRIMARY : ButtonStyle::SECONDARY, btnDocs.Contains(mouseX, mouseY));
        DrawButton(g, btnNet,  "Net",  activePage == 2 ? ButtonStyle::PRIMARY : ButtonStyle::SECONDARY, btnNet.Contains(mouseX, mouseY));

        // Address Bar
        g.FillRoundedRect(urlBar.x, urlBar.y, urlBar.w, urlBar.h, 8, 0xFF0A101D);
        g.DrawRoundedRect(urlBar.x, urlBar.y, urlBar.w, urlBar.h, 8, urlFocused ? Theme::BORDER_FOCUS : Theme::BORDER_GLASS);
        g.DrawText(urlBar.x + 12, urlBar.y + 8, urlBuf, Theme::TEXT_PRIMARY, 1);
        if (urlFocused) {
            int cx = urlBar.x + 12 + MorphicAPI::Graphics::MeasureTextWidth(urlBuf, 1) + 2;
            if (cx < urlBar.x + urlBar.w - 8) g.FillRect(cx, urlBar.y + 7, 2, 18, Theme::ACCENT_CYAN);
        }

        DrawButton(g, btnGo, "Go", ButtonStyle::PRIMARY, btnGo.Contains(mouseX, mouseY));

        // Viewport Card
        Rect view = {16, 66, (int)width - 32, (int)height - 108};
        DrawCard(g, view, 12, 0xFF111827);

        int vx = view.x + 22;
        int vw = view.w - 44;
        int vy = view.y + 20;

        if (activePage == 0) {
            g.DrawText(vx, vy, "Morphic Web Engine", Theme::ACCENT_CYAN, 2);
            vy += 42;
            g.DrawText(vx, vy, "Native Ring-3 HTML/UI browser + Edge WebView2 Host Bridge ready.", Theme::TEXT_SECONDARY, 1);
            vy += 32;

            Rect c1 = {vx, vy, (vw - 16) / 2, 140};
            Rect c2 = {vx + (vw - 16) / 2 + 16, vy, (vw - 16) / 2, 140};
            DrawCard(g, c1, 10, Theme::SURFACE_CARD);
            DrawCard(g, c2, 10, Theme::SURFACE_CARD);

            g.DrawText(c1.x + 16, c1.y + 14, "NATIVE RENDERER", Theme::SUCCESS, 1);
            g.DrawText(c1.x + 16, c1.y + 40, "Zero-dependency MorphicUI", Theme::TEXT_PRIMARY, 1);
            g.DrawText(c1.x + 16, c1.y + 62, "60-240 Hz VSync support", Theme::TEXT_SECONDARY, 1);
            g.DrawText(c1.x + 16, c1.y + 84, "Ring-3 isolated process", Theme::TEXT_SECONDARY, 1);
            DrawBadge(g, c1.x + 16, c1.y + 108, "ACTIVE", Theme::SUCCESS, Theme::SUCCESS_BG);

            g.DrawText(c2.x + 16, c2.y + 14, "EDGE WEBVIEW2 BRIDGE", Theme::ACCENT_CYAN, 1);
            g.DrawText(c2.x + 16, c2.y + 40, "Host-assisted Chromium/Edge", Theme::TEXT_PRIMARY, 1);
            g.DrawText(c2.x + 16, c2.y + 62, "Run: tools/webview_bridge.py", Theme::TEXT_SECONDARY, 1);
            g.DrawText(c2.x + 16, c2.y + 84, "Routes over Universal Net HAL", Theme::TEXT_SECONDARY, 1);
            DrawBadge(g, c2.x + 16, c2.y + 108, netInfo.link_up ? "NIC READY" : "OFFLINE",
                      netInfo.link_up ? Theme::ACCENT_CYAN : Theme::WARNING,
                      netInfo.link_up ? Theme::ACCENT_GLOW : Theme::WARNING_BG);

            vy += 160;
            g.DrawText(vx, vy, "Quick Links:  morphic://home   |   morphic://sdk   |   morphic://net", Theme::TEXT_MUTED, 1);
        } else if (activePage == 1) {
            g.DrawText(vx, vy, "Morphic Multi-Language SDK", Theme::ACCENT_CYAN, 2);
            vy += 42;
            DrawCard(g, {vx, vy, vw, 230}, 10, Theme::SURFACE_CARD);
            int ty = vy + 18;
            g.DrawText(vx + 18, ty, "Create native Ring-3 .mpk apps in 4 languages:", Theme::TEXT_PRIMARY, 1); ty += 30;
            g.DrawText(vx + 18, ty, "1. C++    : python tools/new_app.py MyApp --lang cpp", Theme::SUCCESS, 1); ty += 26;
            g.DrawText(vx + 18, ty, "2. Rust   : python tools/new_app.py MyApp --lang rust  (#![no_std])", Theme::ACCENT_CYAN, 1); ty += 26;
            g.DrawText(vx + 18, ty, "3. Lua    : python tools/new_app.py MyApp --lang lua   (MorphicLua)", Theme::WARNING, 1); ty += 26;
            g.DrawText(vx + 18, ty, "4. Python : python tools/new_app.py MyApp --lang python (AOT x86_64)", 0xFFC084FC, 1); ty += 36;
            g.DrawText(vx + 18, ty, "Hardware APIs: MorphicHW::GPU / Display / Storage / Network / Package", Theme::TEXT_SECONDARY, 1);
        } else {
            g.DrawText(vx, vy, "Universal Network HAL", Theme::SUCCESS, 2);
            vy += 42;
            DrawCard(g, {vx, vy, vw, 190}, 10, Theme::SURFACE_CARD);
            int ty = vy + 20;
            DrawKeyValue(g, vx + 18, ty, vw - 36, "Active NIC Driver", netInfo.present ? netInfo.driver_name : "None detected", Theme::ACCENT_CYAN); ty += 28;
            DrawKeyValue(g, vx + 18, ty, vw - 36, "Link Status", netInfo.link_up ? "UP (Connected)" : "DOWN", netInfo.link_up ? Theme::SUCCESS : Theme::WARNING); ty += 28;
            DrawKeyValue(g, vx + 18, ty, vw - 36, "GPU Acceleration", gpuInfo.vendor_id ? gpuInfo.vendor_name : "Software", Theme::TEXT_PRIMARY); ty += 28;
            DrawKeyValue(g, vx + 18, ty, vw - 36, "WebView2 Bridge", "ws://10.0.2.2:8765 (QEMU/VBox host)", Theme::TEXT_SECONDARY);
        }

        // Bottom Status Strip
        g.DrawText(20, (int)height - 28,
                   netInfo.link_up ? "Status: Universal Network HAL Online | MorphicWeb Ready"
                                   : "Status: Local Mode (morphic://) | Attach e1000/VirtIO-Net for WebView2 Bridge",
                   Theme::TEXT_MUTED, 1);
    }

    void OnMouseDown(int x, int y, int btn) override {
        (void)btn;
        urlFocused = urlBar.Contains(x, y);
        if (btnHome.Contains(x, y)) { activePage = 0; SetUrl("morphic://home"); Invalidate(); return; }
        if (btnDocs.Contains(x, y)) { activePage = 1; SetUrl("morphic://sdk");  Invalidate(); return; }
        if (btnNet.Contains(x, y))  { activePage = 2; SetUrl("morphic://net");  MorphicHW::Network::GetInfo(&netInfo); Invalidate(); return; }
        if (btnGo.Contains(x, y))   { NavigateToUrl(); Invalidate(); return; }
        Invalidate();
    }

    void OnKeyDown(char c) override {
        if (!urlFocused) return;
        if (c == '\n' || c == '\r') { NavigateToUrl(); Invalidate(); return; }
        if (c == '\b' || c == 127) {
            if (urlLen > 0) { urlBuf[--urlLen] = '\0'; Invalidate(); }
            return;
        }
        if (c >= 32 && c <= 126 && urlLen < 90) {
            urlBuf[urlLen++] = c;
            urlBuf[urlLen] = '\0';
            Invalidate();
        }
    }

private:
    char urlBuf[96];
    int urlLen;
    bool urlFocused;
    int activePage;
    MorphicNetInfo netInfo = {};
    MorphicVkPhysicalDeviceInfo gpuInfo = {};
    MorphicUI::Rect btnHome, btnDocs, btnNet, urlBar, btnGo;

    void SetUrl(const char* u) {
        MorphicAPI::Helpers::strcpy_s(urlBuf, u, sizeof(urlBuf));
        urlLen = (int)MorphicAPI::Helpers::strlen(urlBuf);
    }

    void NavigateToUrl() {
        if (MorphicAPI::Helpers::strcmp(urlBuf, "morphic://sdk") == 0) activePage = 1;
        else if (MorphicAPI::Helpers::strcmp(urlBuf, "morphic://net") == 0) activePage = 2;
        else activePage = 0;
    }
};
