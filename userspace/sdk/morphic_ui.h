#pragma once
#include "morphic_api.h"

// ============================================================================
// MorphicUI v2 - Glass Dark Design System & Reusable Widget Toolkit
// Unified styling for Morphic OS native .mpk applications.
// ============================================================================

namespace MorphicUI {

    // Glass Dark Palette (macOS / Hyprland inspired)
    namespace Theme {
        static constexpr uint32_t BG_CANVAS      = 0xFF0B101B; // Deep obsidian navy
        static constexpr uint32_t BG_TOP         = 0xFF0F172A; // Slate 900
        static constexpr uint32_t BG_BOTTOM      = 0xFF090D16; // Deep night
        static constexpr uint32_t SURFACE_GLASS  = 0xEB131B2E; // Translucent glass panel
        static constexpr uint32_t SURFACE_CARD   = 0xFF172033; // Elevated card
        static constexpr uint32_t SURFACE_SUBTLE = 0xFF1E293B; // Control fill
        static constexpr uint32_t SURFACE_HOVER  = 0xFF28354D; // Hovered control
        static constexpr uint32_t SURFACE_ACTIVE = 0xFF334155; // Pressed control

        static constexpr uint32_t BORDER_GLASS   = 0x2AFFFFFF; // Subtle translucent rim
        static constexpr uint32_t BORDER_FOCUS   = 0xAA38BDF8; // Cyan focus ring
        static constexpr uint32_t DIVIDER        = 0x1EFFFFFF;

        static constexpr uint32_t ACCENT_CYAN    = 0xFF38BDF8; // Primary accent
        static constexpr uint32_t ACCENT_CYAN_DK = 0xFF0284C7; // Gradient bottom
        static constexpr uint32_t ACCENT_INDIGO  = 0xFF6366F1; // Secondary accent
        static constexpr uint32_t ACCENT_GLOW    = 0x3338BDF8;

        static constexpr uint32_t SUCCESS        = 0xFF34D399;
        static constexpr uint32_t SUCCESS_BG     = 0x2634D399;
        static constexpr uint32_t WARNING        = 0xFFFBBF24;
        static constexpr uint32_t WARNING_BG     = 0x26FBBF24;
        static constexpr uint32_t DANGER         = 0xFFFB7185;
        static constexpr uint32_t DANGER_BG      = 0x28FB7185;

        static constexpr uint32_t TEXT_PRIMARY   = 0xFFF1F5F9;
        static constexpr uint32_t TEXT_SECONDARY = 0xFF94A3B8;
        static constexpr uint32_t TEXT_MUTED     = 0xFF64748B;

        static constexpr int RADIUS_SM = 6;
        static constexpr int RADIUS_MD = 10;
        static constexpr int RADIUS_LG = 14;
    }

    struct Rect {
        int x, y, w, h;

        bool Contains(int px, int py) const {
            return px >= x && px < (x + w) && py >= y && py < (y + h);
        }
    };

    enum class ButtonStyle {
        SECONDARY = 0,
        PRIMARY   = 1,
        SUCCESS   = 2,
        DANGER    = 3,
        GHOST     = 4
    };

    // Full-window Glass Dark gradient backdrop
    static inline void DrawWindowBackdrop(MorphicAPI::Graphics& g) {
        g.FillGradientV(0, 0, (int)g.GetWidth(), (int)g.GetHeight(), Theme::BG_TOP, Theme::BG_BOTTOM);
    }

    // Modern top header bar inside an app window
    static inline void DrawHeaderBar(MorphicAPI::Graphics& g, const char* title, const char* subtitle = nullptr, int height = 52) {
        int w = (int)g.GetWidth();
        g.FillGradientV(0, 0, w, height, 0xFF162036, 0xFF111829);
        g.DrawHLine(0, height - 1, w, Theme::BORDER_GLASS);
        // Accent pill indicator
        g.FillRoundedRect(16, (height - 20) / 2, 4, 20, 2, Theme::ACCENT_CYAN);
        g.DrawText(28, subtitle ? 9 : (height - 16) / 2, title, Theme::TEXT_PRIMARY, 1);
        if (subtitle) {
            g.DrawText(28, 28, subtitle, Theme::TEXT_SECONDARY, 1);
        }
    }

    // Elevated glass card with subtle border
    static inline void DrawCard(MorphicAPI::Graphics& g, const Rect& r, int radius = Theme::RADIUS_MD, uint32_t fill = Theme::SURFACE_CARD) {
        g.FillRoundedRect(r.x, r.y, r.w, r.h, radius, fill);
        g.DrawRoundedRect(r.x, r.y, r.w, r.h, radius, Theme::BORDER_GLASS);
    }

    // Pill status badge (e.g. "LINKED", "VK 1.3", "INSTALLED")
    static inline void DrawBadge(MorphicAPI::Graphics& g, int x, int y, const char* label,
                                 uint32_t fgColor = Theme::ACCENT_CYAN, uint32_t bgColor = Theme::ACCENT_GLOW) {
        int tw = MorphicAPI::Graphics::MeasureTextWidth(label, 1);
        int bw = tw + 18;
        int bh = 22;
        g.FillRoundedRect(x, y, bw, bh, 11, bgColor);
        g.DrawRoundedRect(x, y, bw, bh, 11, MorphicAPI::Color::WithAlpha(fgColor, 0x66));
        g.DrawTextCentered(x, y, bw, bh, label, fgColor, 1);
    }

    // Interactive rounded button with hover/primary/danger styling
    static inline void DrawButton(MorphicAPI::Graphics& g, const Rect& r, const char* label,
                                  ButtonStyle style = ButtonStyle::SECONDARY, bool hovered = false, bool disabled = false) {
        uint32_t topCol = Theme::SURFACE_SUBTLE;
        uint32_t botCol = 0xFF172133;
        uint32_t borderCol = Theme::BORDER_GLASS;
        uint32_t textCol = Theme::TEXT_PRIMARY;

        if (disabled) {
            topCol = 0xFF141B29;
            botCol = 0xFF141B29;
            textCol = Theme::TEXT_MUTED;
        } else if (style == ButtonStyle::PRIMARY) {
            topCol = hovered ? 0xFF56CCF8 : Theme::ACCENT_CYAN;
            botCol = hovered ? 0xFF0EA5E9 : Theme::ACCENT_CYAN_DK;
            borderCol = 0x55FFFFFF;
            textCol = 0xFF06101C;
        } else if (style == ButtonStyle::SUCCESS) {
            topCol = hovered ? 0xFF4ADE80 : Theme::SUCCESS;
            botCol = hovered ? 0xFF10B981 : 0xFF059669;
            borderCol = 0x44FFFFFF;
            textCol = 0xFF051811;
        } else if (style == ButtonStyle::DANGER) {
            topCol = hovered ? 0xFFFDA4AF : Theme::DANGER;
            botCol = hovered ? 0xFFF43F5E : 0xFFE11D48;
            borderCol = 0x44FFFFFF;
            textCol = 0xFFFFFFFF;
        } else if (style == ButtonStyle::GHOST) {
            topCol = hovered ? Theme::SURFACE_HOVER : 0x00000000;
            botCol = topCol;
            borderCol = hovered ? Theme::BORDER_GLASS : 0x00000000;
            textCol = hovered ? Theme::TEXT_PRIMARY : Theme::TEXT_SECONDARY;
        } else if (hovered) {
            topCol = Theme::SURFACE_HOVER;
            botCol = 0xFF1E293B;
            borderCol = Theme::BORDER_FOCUS;
        }

        if ((topCol >> 24) != 0) {
            g.FillRoundedGradientV(r.x, r.y, r.w, r.h, Theme::RADIUS_SM, topCol, botCol);
        }
        if ((borderCol >> 24) != 0) {
            g.DrawRoundedRect(r.x, r.y, r.w, r.h, Theme::RADIUS_SM, borderCol);
        }
        g.DrawTextCentered(r.x, r.y, r.w, r.h, label, textCol, 1);
    }

    // Modern rounded progress bar (percent 0..100)
    static inline void DrawProgressBar(MorphicAPI::Graphics& g, const Rect& r, int percent,
                                       uint32_t fillTop = Theme::ACCENT_CYAN, uint32_t fillBot = Theme::ACCENT_INDIGO) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        int rad = r.h / 2;
        g.FillRoundedRect(r.x, r.y, r.w, r.h, rad, 0xFF0D1422);
        g.DrawRoundedRect(r.x, r.y, r.w, r.h, rad, Theme::BORDER_GLASS);
        int fillW = (r.w * percent) / 100;
        if (fillW > 2) {
            g.FillRoundedGradientV(r.x, r.y, fillW, r.h, rad, fillTop, fillBot);
        }
    }

    // Key-Value telemetry row inside a card
    static inline void DrawKeyValue(MorphicAPI::Graphics& g, int x, int y, int width,
                                    const char* key, const char* value,
                                    uint32_t valueColor = Theme::TEXT_PRIMARY) {
        g.DrawText(x, y, key, Theme::TEXT_SECONDARY, 1);
        int vw = MorphicAPI::Graphics::MeasureTextWidth(value, 1);
        g.DrawText(x + width - vw, y, value, valueColor, 1);
    }

}
