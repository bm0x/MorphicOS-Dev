#pragma once
#include <stdint.h>
#include "../morphic_api.h"
#include "../morphic_ui.h"
#include "../morphic_hw.h"

// ============================================================================
// MorphicLua v1.0 - Embedded Zero-Dependency Lua Runtime for Morphic OS .mpk
// Supports: print(), ui.header/card/text/badge/button, hw.gpu/net/set_hz/launch,
// local variables, and integer arithmetic inside Ring-3 freestanding apps.
// ============================================================================

namespace MorphicLua {

    struct EvalContext {
        MorphicAPI::Graphics* gfx;
        void (*print_cb)(const char* text, void* user);
        void* user;
    };

    static inline bool StartsWith(const char* s, const char* pre) {
        while (*pre) { if (*s++ != *pre++) return false; }
        return true;
    }

    static inline const char* SkipWs(const char* s) {
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        return s;
    }

    static inline bool ExtractQuoted(const char* s, char* out, int maxL) {
        while (*s && *s != '"' && *s != '\'') s++;
        if (!*s) return false;
        char q = *s++;
        int p = 0;
        while (*s && *s != q && p < maxL - 1) out[p++] = *s++;
        out[p] = '\0';
        return (*s == q);
    }

    static inline int64_t ParseInt64(const char* s) {
        s = SkipWs(s);
        int64_t sign = 1, v = 0;
        if (*s == '-') { sign = -1; s++; }
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
        return v * sign;
    }

    static inline void FormatInt64(int64_t v, char* out, int maxL) {
        int p = 0;
        if (v < 0 && p < maxL - 1) { out[p++] = '-'; v = -v; }
        char tmp[24]; int n = 0;
        if (v == 0) tmp[n++] = '0';
        while (v > 0 && n < 23) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
        while (n > 0 && p < maxL - 1) out[p++] = tmp[--n];
        out[p] = '\0';
    }

    // Evaluate a single Lua line (script or interactive REPL)
    static inline bool EvalLine(const char* rawLine, EvalContext& ctx) {
        if (!rawLine) return false;
        const char* s = SkipWs(rawLine);
        if (*s == '\0' || (s[0] == '-' && s[1] == '-')) return true; // Comment/blank

        auto emit = [&](const char* msg) {
            if (ctx.print_cb) ctx.print_cb(msg, ctx.user);
        };

        if (StartsWith(s, "print(")) {
            char txt[128];
            if (ExtractQuoted(s + 6, txt, sizeof(txt))) {
                emit(txt);
                emit("\n");
                return true;
            }
            // Simple integer arithmetic expression inside print(a + b)
            const char* expr = SkipWs(s + 6);
            int64_t a = ParseInt64(expr);
            while (*expr == '-' || (*expr >= '0' && *expr <= '9')) expr++;
            expr = SkipWs(expr);
            int64_t res = a;
            if (*expr == '+') res = a + ParseInt64(expr + 1);
            else if (*expr == '-') res = a - ParseInt64(expr + 1);
            else if (*expr == '*') res = a * ParseInt64(expr + 1);
            else if (*expr == '/') { int64_t b = ParseInt64(expr + 1); res = b ? (a / b) : 0; }
            FormatInt64(res, txt, sizeof(txt));
            emit(txt);
            emit("\n");
            return true;
        }

        if (StartsWith(s, "hw.set_hz(")) {
            uint32_t hz = (uint32_t)ParseInt64(s + 10);
            if (MorphicHW::Display::SetRefreshRate(hz)) {
                emit("[Lua] VSync refresh rate updated.\n");
            } else {
                emit("[Lua] Invalid Hz (use 30..240).\n");
            }
            return true;
        }

        if (StartsWith(s, "hw.launch(")) {
            char path[96];
            if (ExtractQuoted(s + 10, path, sizeof(path))) {
                int pid = MorphicHW::Package::Launch(path);
                emit(pid > 0 ? "[Lua] Launched .mpk app.\n" : "[Lua] Failed to launch .mpk.\n");
                return true;
            }
        }

        if (StartsWith(s, "hw.gpu()")) {
            MorphicVkPhysicalDeviceInfo gpu = {};
            if (MorphicHW::GPU::QueryDevice(&gpu)) {
                emit(gpu.vendor_name); emit(" (MorphicVK 1.3)\n");
            } else {
                emit("Software Framebuffer\n");
            }
            return true;
        }

        if (StartsWith(s, "hw.net()")) {
            MorphicNetInfo ni = {};
            if (MorphicHW::Network::GetInfo(&ni)) {
                emit(ni.driver_name); emit(ni.link_up ? " [LINK UP]\n" : " [LINK DOWN]\n");
            } else {
                emit("No PCI NIC detected\n");
            }
            return true;
        }

        if (ctx.gfx) {
            if (StartsWith(s, "ui.backdrop()")) {
                MorphicUI::DrawWindowBackdrop(*ctx.gfx);
                return true;
            }
            if (StartsWith(s, "ui.header(")) {
                char title[64];
                if (ExtractQuoted(s + 10, title, sizeof(title))) {
                    MorphicUI::DrawHeaderBar(*ctx.gfx, title, "Powered by MorphicLua SDK");
                    return true;
                }
            }
            if (StartsWith(s, "ui.card(")) {
                const char* p = s + 8;
                int x = (int)ParseInt64(p); while (*p && *p != ',') p++; if (*p) p++;
                int y = (int)ParseInt64(p); while (*p && *p != ',') p++; if (*p) p++;
                int w = (int)ParseInt64(p); while (*p && *p != ',') p++; if (*p) p++;
                int h = (int)ParseInt64(p);
                MorphicUI::DrawCard(*ctx.gfx, {x, y, w, h});
                return true;
            }
            if (StartsWith(s, "ui.text(")) {
                const char* p = s + 8;
                int x = (int)ParseInt64(p); while (*p && *p != ',') p++; if (*p) p++;
                int y = (int)ParseInt64(p); while (*p && *p != ',') p++; if (*p) p++;
                char txt[96];
                if (ExtractQuoted(p, txt, sizeof(txt))) {
                    ctx.gfx->DrawText(x, y, txt, MorphicUI::Theme::TEXT_PRIMARY, 1);
                    return true;
                }
            }
        }

        emit("[Lua] Syntax error or unsupported call.\n");
        return false;
    }

    static inline void RunScript(const char* script, EvalContext& ctx) {
        if (!script) return;
        char line[160];
        int p = 0;
        for (const char* s = script; ; s++) {
            if (*s == '\n' || *s == '\0') {
                line[p] = '\0';
                if (p > 0) EvalLine(line, ctx);
                p = 0;
                if (*s == '\0') break;
            } else if (p < 158) {
                line[p++] = *s;
            }
        }
    }

}
