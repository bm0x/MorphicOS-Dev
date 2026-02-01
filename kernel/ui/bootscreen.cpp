#include "bootscreen.h"
#include "../hal/video/graphics.h"
#include "../hal/video/font_renderer.h"
#include "../utils/std.h"

namespace BootScreen {

    static uint32_t currentProgress = 0;
    static const uint32_t BG_COLOR = 0xFF1E1E2E; // Catppuccin Base (Dark Blue-Grey)
    static const uint32_t ACCENT_COLOR = 0xFF89B4FA; // Catppuccin Blue
    static const uint32_t BAR_BG_COLOR = 0xFF313244; // Surface0
    static const uint32_t TEXT_COLOR = 0xFFCDD6F4; // Text White

    static uint32_t width = 0;
    static uint32_t height = 0;

    void Init() {
        width = Graphics::GetWidth();
        height = Graphics::GetHeight();

        // 1. Clear Screen to generic background
        Graphics::FillRect(0, 0, width, height, BG_COLOR);

        // 2. Draw "Logo" (Centered Text for now)
        const char* title = "MORPHIC OS";
        uint32_t titleLen = FontRenderer::GetTextWidth(title);
        uint32_t titleX = (width - titleLen) / 2;
        uint32_t titleY = (height / 2) - 50;

        // Draw basic shadow
        FontRenderer::DrawText(Graphics::GetDrawBuffer(), width, height, titleX + 2, titleY + 2, title, 0xFF11111B, BG_COLOR);
        // Draw Main Text
        FontRenderer::DrawText(Graphics::GetDrawBuffer(), width, height, titleX, titleY, title, 0xFFFFFFFF, BG_COLOR);

        // 3. Draw initial bar container
        uint32_t barW = width / 3;
        uint32_t barH = 10;
        uint32_t barX = (width - barW) / 2;
        uint32_t barY = (height / 2) + 20;

        Graphics::FillRect(barX, barY, barW, barH, BAR_BG_COLOR);

        // Render whole screen
        Graphics::Flip();
    }

    void Update(uint32_t percent, const char* message) {
        if (percent > 100) percent = 100;
        // currentProgress = percent; // Storing int now if needed, or remove static float

        // Coordinates
        uint32_t barW = width / 3;
        uint32_t barH = 10;
        uint32_t barX = (width - barW) / 2;
        uint32_t barY = (height / 2) + 20;

        // 1. Update Bar Fill using Dirty Rect (Integer Math)
        // formula: (barW * percent) / 100
        uint32_t fillW = (barW * percent) / 100;
        Graphics::FillRect(barX, barY, fillW, barH, ACCENT_COLOR);
        
        // 2. Clear Text Area (below bar)
        uint32_t textY = barY + 20;
        // Assume text height 16px, padding 4px
        Graphics::FillRect(0, textY, width, 20, BG_COLOR);

        // 3. Draw Message Centered
        uint32_t msgLen = FontRenderer::GetTextWidth(message);
        uint32_t msgX = (width - msgLen) / 2;
        
        FontRenderer::DrawText(Graphics::GetDrawBuffer(), width, height, msgX, textY, message, TEXT_COLOR, BG_COLOR);

        // 4. Smart Flip (Only update the changing area)
        // Calculating union of bar area and text area
        // Bar Y to Text Y + 20
        uint32_t dirtyY = barY;
        uint32_t dirtyH = (textY + 20) - barY;
        
        // We flip the whole width of the bar/text strip for simplicity
        // But to be safer with "previous text was wider", we flip the whole width section or just clear wider.
        // We cleared the whole width set in step 2.
        Graphics::FlipRect(0, dirtyY, width, dirtyH);
    }
    
    void Log(const char* message) {
        Update(currentProgress, message);
    }

    void Finish() {
        // Force full refresh to clear any artifacts
        Graphics::FillRect(0, 0, Graphics::GetWidth(), Graphics::GetHeight(), 0xFF000000); // Clear to Black
        Graphics::Flip();
    }
}
