#include "compositor.h"

// Static member storage
uint32_t* Compositor::frontBuffer = nullptr;
uint32_t* Compositor::backBuffer = nullptr;
int Compositor::width = 0;
int Compositor::height = 0;
int Compositor::bpp = 32; // Assuming 32

bool Compositor::clipEnabled = false;
int Compositor::clipX = 0;
int Compositor::clipY = 0;
int Compositor::clipW = 0;
int Compositor::clipH = 0;

static inline int i_max(int a, int b) { return a > b ? a : b; }
static inline int i_min(int a, int b) { return a < b ? a : b; }

static inline char ToUpperAscii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

bool Compositor::Initialize() {
    // 1. Get Screen Geometry
    uint64_t info = sys_get_screen_info();
    width = (info >> 32) & 0xFFFFFFFF;
    height = info & 0xFFFFFFFF;
    
    if (width == 0 || height == 0) return false;
    
    // 2. Map Front Buffer (VRAM)
    frontBuffer = (uint32_t*)sys_video_map();
    if (!frontBuffer) return false;
    
    // 3. Allocate Back Buffer (RAM)
    // Size = Width * Height * 4 bytes
    uint64_t size = (uint64_t)width * height * 4;
    uint64_t backAddr = sys_alloc_backbuffer(size);
    
    if (backAddr == 0) return false;
    
    backBuffer = (uint32_t*)backAddr;
    
    return true;
}

void Compositor::Clear(uint32_t color) {
    if (!backBuffer) return;
    
    uint64_t total_pixels = (uint64_t)width * height;
    for (uint64_t i = 0; i < total_pixels; i++) {
        backBuffer[i] = color;
    }
}

void Compositor::SetClip(int x, int y, int w, int h) {
    clipEnabled = true;
    clipX = x; clipY = y; clipW = w; clipH = h;
}

void Compositor::ClearClip() {
    clipEnabled = false;
}

bool Compositor::IntersectsClip(int x, int y, int w, int h) {
    if (!clipEnabled) return true;
    int x2 = x + w;
    int y2 = y + h;
    int cx2 = clipX + clipW;
    int cy2 = clipY + clipH;
    return !(x2 <= clipX || x >= cx2 || y2 <= clipY || y >= cy2);
}

void Compositor::FillRectClipped(int x, int y, int w, int h, uint32_t color) {
    if (!backBuffer) return;
    if (w <= 0 || h <= 0) return;

    // Clip to screen
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;

    // Clip to dirty
    if (clipEnabled) {
        int nx = i_max(x, clipX);
        int ny = i_max(y, clipY);
        int nx2 = i_min(x + w, clipX + clipW);
        int ny2 = i_min(y + h, clipY + clipH);
        x = nx; y = ny; w = nx2 - nx; h = ny2 - ny;
        if (w <= 0 || h <= 0) return;
    }

    for (int j = 0; j < h; j++) {
        uint32_t* row = &backBuffer[(y + j) * width + x];
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void Compositor::DrawRectClipped(int x, int y, int w, int h, uint32_t color) {
    // Border rectangle: 1px lines
    FillRectClipped(x, y, w, 1, color);
    FillRectClipped(x, y + h - 1, w, 1, color);
    FillRectClipped(x, y, 1, h, color);
    FillRectClipped(x + w - 1, y, 1, h, color);
}

void Compositor::DrawRect(int x, int y, int w, int h, uint32_t color) {
    FillRectClipped(x, y, w, h, color);
}

// Simple Arrow Bitmap (12x18) code: 0=Trans, 1=White, 2=Black
static const uint8_t cursor_w = 12;
static const uint8_t cursor_h = 18;
static const uint8_t cursor_bitmap[] = {
    2,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,
    2,1,1,1,1,1,2,2,2,2,2,0,
    2,1,1,2,1,1,2,0,0,0,0,0,
    2,1,2,0,2,1,1,2,0,0,0,0,
    2,2,0,0,2,1,1,2,0,0,0,0,
    2,0,0,0,0,2,1,1,2,0,0,0,
    0,0,0,0,0,2,1,1,2,0,0,0,
    0,0,0,0,0,0,2,2,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0
};

void Compositor::DrawCursor(int x, int y) {
    DrawCursorClipped(x, y);
}

void Compositor::DrawCursorClipped(int x, int y) {
    for (int cy = 0; cy < cursor_h; cy++) {
        for (int cx = 0; cx < cursor_w; cx++) {
            int px = x + cx;
            int py = y + cy;
            
            if (px < 0 || px >= width || py < 0 || py >= height) continue;
            if (clipEnabled) {
                if (px < clipX || px >= clipX + clipW || py < clipY || py >= clipY + clipH) continue;
            }
            
            uint8_t code = cursor_bitmap[cy * cursor_w + cx];
            uint32_t color = 0;
            
            if (code == 1) color = 0xFFFFFFFF; // White
            else if (code == 2) color = 0xFF000000; // Black
            else continue; // Transparent
            
            backBuffer[py * width + px] = color;
        }
    }
}

bool Compositor::SwapBuffers() {
    if (!backBuffer || !frontBuffer) return false;

    // Present via kernel: optimized blit + best-effort VSync wait.
    // This is substantially faster than copying pixel-by-pixel in userspace.
    return sys_video_flip(backBuffer) != 0;
}

bool Compositor::SwapBuffersRect(int x, int y, int w, int h) {
    if (!backBuffer || !frontBuffer) return false;
    if (w <= 0 || h <= 0) return false;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return false;

    uint64_t xy = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t wh = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
    return sys_video_flip_rect(backBuffer, xy, wh) != 0;
}

static void DrawSevenSegSegment(uint32_t* buf, int bw, int bh, bool clipEnabled, int clipX, int clipY, int clipW, int clipH,
                                int x, int y, int w, int h, uint32_t color)
{
    if (!buf) return;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > bw) w = bw - x;
    if (y + h > bh) h = bh - y;
    if (w <= 0 || h <= 0) return;
    if (clipEnabled) {
        int nx = i_max(x, clipX);
        int ny = i_max(y, clipY);
        int nx2 = i_min(x + w, clipX + clipW);
        int ny2 = i_min(y + h, clipY + clipH);
        x = nx; y = ny; w = nx2 - nx; h = ny2 - ny;
        if (w <= 0 || h <= 0) return;
    }
    for (int j = 0; j < h; j++) {
        uint32_t* row = &buf[(y + j) * bw + x];
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void Compositor::DrawSevenSegDigit(int x, int y, int scale, int digit, uint32_t onColor, uint32_t offColor) {
    // 7-seg: segments A,B,C,D,E,F,G
    // Bitmap per digit
    static const uint8_t seg[10] = {
        0b1111110, // 0
        0b0110000, // 1
        0b1101101, // 2
        0b1111001, // 3
        0b0110011, // 4
        0b1011011, // 5
        0b1011111, // 6
        0b1110000, // 7
        0b1111111, // 8
        0b1111011, // 9
    };
    if (digit < 0 || digit > 9) digit = 0;
    uint8_t mask = seg[digit];

    int t = 2 * scale;                 // thickness
    int l = 6 * scale;                 // segment length
    int gap = 2 * scale;

    // A: top
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x + gap, y, l, t, (mask & 0b1000000) ? onColor : offColor);
    // B: upper-right
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x + gap + l, y + gap, t, l, (mask & 0b0100000) ? onColor : offColor);
    // C: lower-right
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x + gap + l, y + gap + l + t, t, l, (mask & 0b0010000) ? onColor : offColor);
    // D: bottom
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x + gap, y + 2 * gap + 2 * l + t, l, t, (mask & 0b0001000) ? onColor : offColor);
    // E: lower-left
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x, y + gap + l + t, t, l, (mask & 0b0000100) ? onColor : offColor);
    // F: upper-left
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x, y + gap, t, l, (mask & 0b0000010) ? onColor : offColor);
    // G: middle
    DrawSevenSegSegment(backBuffer, width, height, clipEnabled, clipX, clipY, clipW, clipH,
                        x + gap, y + gap + l, l, t, (mask & 0b0000001) ? onColor : offColor);
}

void Compositor::DrawClockHHMM(int rightX, int y, int scale, uint32_t timeSeconds) {
    // timeSeconds is monotonic since boot; show HH:MM modulo 24h.
    uint32_t total = timeSeconds % (24 * 3600);
    uint32_t hh = total / 3600;
    uint32_t mm = (total / 60) % 60;

    int d0 = (int)(hh / 10);
    int d1 = (int)(hh % 10);
    int d2 = (int)(mm / 10);
    int d3 = (int)(mm % 10);

    uint32_t on = 0xFFEAEAEA;
    uint32_t off = 0xFF2A2A2A;

    int digitW = (2 * scale) + (2 * scale) + (6 * scale) + (2 * scale); // approx box
    int spacing = 3 * scale;

    int x = rightX - (digitW * 4 + spacing * 3 + 6 * scale);

    DrawSevenSegDigit(x, y, scale, d0, on, off);
    x += digitW + spacing;
    DrawSevenSegDigit(x, y, scale, d1, on, off);

    // Colon
    int cx = x + digitW + spacing / 2;
    int cy = y + 6 * scale;
    FillRectClipped(cx, cy, 2 * scale, 2 * scale, on);
    FillRectClipped(cx, cy + 8 * scale, 2 * scale, 2 * scale, on);
    x += digitW + spacing;

    DrawSevenSegDigit(x, y, scale, d2, on, off);
    x += digitW + spacing;
    DrawSevenSegDigit(x, y, scale, d3, on, off);
}

static uint8_t Glyph5x7(char ch, int row)
{
    // Returns 5-bit row (bit4..bit0).
    // Minimal glyph set for UI/system info. Unsupported -> '?'
    ch = ToUpperAscii(ch);

    // Digits
    switch (ch)
    {
        case '0': { static const uint8_t r[7]={0x1E,0x11,0x13,0x15,0x19,0x11,0x1E}; return r[row]; }
        case '1': { static const uint8_t r[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return r[row]; }
        case '2': { static const uint8_t r[7]={0x1E,0x01,0x01,0x1E,0x10,0x10,0x1F}; return r[row]; }
        case '3': { static const uint8_t r[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return r[row]; }
        case '4': { static const uint8_t r[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return r[row]; }
        case '5': { static const uint8_t r[7]={0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return r[row]; }
        case '6': { static const uint8_t r[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x1E}; return r[row]; }
        case '7': { static const uint8_t r[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return r[row]; }
        case '8': { static const uint8_t r[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return r[row]; }
        case '9': { static const uint8_t r[7]={0x1E,0x11,0x11,0x1F,0x01,0x01,0x0E}; return r[row]; }

        // Punctuation
        case ':': { static const uint8_t r[7]={0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return r[row]; }
        case '-': { static const uint8_t r[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return r[row]; }
        case '/': { static const uint8_t r[7]={0x01,0x02,0x04,0x08,0x10,0x00,0x00}; return r[row]; }
        case '.': { static const uint8_t r[7]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return r[row]; }
        case ' ': { static const uint8_t r[7]={0x00,0x00,0x00,0x00,0x00,0x00,0x00}; return r[row]; }

        // Letters used in labels
        case 'A': { static const uint8_t r[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return r[row]; }
        case 'B': { static const uint8_t r[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return r[row]; }
        case 'C': { static const uint8_t r[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return r[row]; }
        case 'D': { static const uint8_t r[7]={0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; return r[row]; }
        case 'E': { static const uint8_t r[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return r[row]; }
        case 'F': { static const uint8_t r[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return r[row]; }
        case 'G': { static const uint8_t r[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return r[row]; }
        case 'H': { static const uint8_t r[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return r[row]; }
        case 'I': { static const uint8_t r[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return r[row]; }
        case 'L': { static const uint8_t r[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return r[row]; }
        case 'M': { static const uint8_t r[7]={0x11,0x1B,0x15,0x11,0x11,0x11,0x11}; return r[row]; }
        case 'N': { static const uint8_t r[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return r[row]; }
        case 'O': { static const uint8_t r[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return r[row]; }
        case 'P': { static const uint8_t r[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return r[row]; }
        case 'R': { static const uint8_t r[7]={0x1E,0x11,0x11,0x1E,0x12,0x11,0x11}; return r[row]; }
        case 'S': { static const uint8_t r[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return r[row]; }
        case 'T': { static const uint8_t r[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return r[row]; }
        case 'U': { static const uint8_t r[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return r[row]; }
        case 'V': { static const uint8_t r[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return r[row]; }
        case 'X': { static const uint8_t r[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return r[row]; }
        case 'Y': { static const uint8_t r[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return r[row]; }

        default:
        {
            static const uint8_t r[7]={0x1E,0x01,0x02,0x04,0x08,0x00,0x08};
            return r[row];
        }
    }
}

void Compositor::DrawChar(int x, int y, char ch, uint32_t color, int scale)
{
    if (!backBuffer) return;
    if (scale < 1) scale = 1;
    for (int row = 0; row < 7; row++)
    {
        uint8_t bits = Glyph5x7(ch, row);
        for (int col = 0; col < 5; col++)
        {
            if ((bits & (1u << (4 - col))) == 0) continue;
            FillRectClipped(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
}

void Compositor::DrawText(int x, int y, const char* text, uint32_t color, int scale)
{
    if (!text) return;
    int cx = x;
    for (const char* p = text; *p; p++)
    {
        char ch = *p;
        if (ch == '\n') { y += (8 * scale); cx = x; continue; }
        DrawChar(cx, y, ch, color, scale);
        cx += 6 * scale;
    }
}

void Compositor::DrawClockText(int rightX, int y, uint32_t hh, uint32_t mm, uint32_t ss, uint32_t dd, uint32_t mo)
{
    char buf[32];
    // HH:MM:SS DD/MM
    buf[0] = (char)('0' + (hh / 10) % 10);
    buf[1] = (char)('0' + (hh % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (mm / 10) % 10);
    buf[4] = (char)('0' + (mm % 10));
    buf[5] = ':';
    buf[6] = (char)('0' + (ss / 10) % 10);
    buf[7] = (char)('0' + (ss % 10));
    buf[8] = ' ';
    buf[9] = (char)('0' + (dd / 10) % 10);
    buf[10] = (char)('0' + (dd % 10));
    buf[11] = '/';
    buf[12] = (char)('0' + (mo / 10) % 10);
    buf[13] = (char)('0' + (mo % 10));
    buf[14] = '\0';

    // Right-align
    int len = 0; while (buf[len]) len++;
    int w = len * 6;
    DrawText(rightX - w, y, buf, 0xFFEAEAEA, 1);
}

void Compositor::RenderMenu(bool menuOpen) {
    if (!menuOpen) return;
    const int taskH = 40;
    const int menuW = 220;
    const int menuH = 180;
    const int x = 10;
    const int y = height - taskH - menuH - 8;

    // Panel
    FillRectClipped(x, y, menuW, menuH, 0xFF141414);
    DrawRectClipped(x, y, menuW, menuH, 0xFF2A2A2A);

    // Minimal items (no text): just blocks
    int itemX = x + 12;
    int itemY = y + 12;
    int itemW = menuW - 24;
    int itemH = 36;
    for (int i = 0; i < 4; i++) {
        FillRectClipped(itemX, itemY, itemW, itemH, 0xFF1E1E1E);
        DrawRectClipped(itemX, itemY, itemW, itemH, 0xFF2A2A2A);
        itemY += itemH + 10;
    }
}

void Compositor::RenderTaskbar(Window* windows, int windowCount, bool menuOpen, const MorphicDateTime& dt) {
    (void)menuOpen;
    const int taskH = 40;
    const int y = height - taskH;

    // Bar background
    FillRectClipped(0, y, width, taskH, 0xFF101010);
    DrawRectClipped(0, y, width, taskH, 0xFF1E1E1E);

    // Start/menu button (minimal)
    FillRectClipped(10, y + 8, 28, 24, 0xFF1A1A1A);
    DrawRectClipped(10, y + 8, 28, 24, 0xFF2A2A2A);

    // Window icons
    int iconX = 48;
    for (int i = 0; i < windowCount; i++) {
        uint32_t c = windows[i].color;
        // desaturate-ish: mix with dark
        uint32_t base = 0xFF1A1A1A;
        uint32_t icon = (c & 0x00FFFFFF) | 0xFF000000;
        FillRectClipped(iconX, y + 8, 24, 24, base);
        FillRectClipped(iconX + 4, y + 12, 16, 16, icon);
        DrawRectClipped(iconX, y + 8, 24, 24, 0xFF2A2A2A);

        // Minimized indicator
        if (windows[i].minimized) {
            FillRectClipped(iconX + 6, y + 26, 12, 2, 0xFFEAEAEA);
        }

        iconX += 30;
    }

    // Clock at right
    // If RTC is valid, use it. Otherwise use 00:00:00 01/01
    uint32_t hh = dt.hour;
    uint32_t mm = dt.minute;
    uint32_t ss = dt.second;
    uint32_t dd = dt.day > 0 ? dt.day : 1;
    uint32_t mo = dt.month > 0 ? dt.month : 1;
    
    DrawClockText(width - 10, y + 12, hh, mm, ss, dd, mo);
}

void Compositor::RenderScene(Window* windows, int windowCount, int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    // Full scene redraw (but DrawRect is clip-aware, so this can be used with dirty rect too)
    // 1. Background
    FillRectClipped(0, 0, width, height, 0xFF181818);

    // 2. Taskbar + menu (drawn by Desktop using helpers; keep here for compatibility)
    // (No-op here; Desktop will call RenderTaskbar/RenderMenu when doing partial redraw)

    // 3. Windows
    const int titleH = 26;
    for (int i = 0; i < windowCount; i++) {
        Window& w = windows[i];
        if (w.minimized) continue;
        if (!IntersectsClip(w.x, w.y, w.width, w.height)) continue;

        // Window body
        FillRectClipped(w.x, w.y, w.width, w.height, 0xFF1C1C1C);
        DrawRectClipped(w.x, w.y, w.width, w.height, 0xFF2A2A2A);

        // Titlebar
        FillRectClipped(w.x, w.y, w.width, titleH, 0xFF121212);
        DrawRectClipped(w.x, w.y, w.width, titleH, 0xFF2A2A2A);

        // Accent line (minimal)
        FillRectClipped(w.x + 1, w.y + titleH - 2, w.width - 2, 1, (w.color & 0x00FFFFFF) | 0xFF000000);

        // Window controls (min/max/close) right side
        int btnSize = 14;
        int pad = 6;
        int bx = w.x + w.width - pad - btnSize;
        int by = w.y + (titleH - btnSize) / 2;

        // Close
        FillRectClipped(bx, by, btnSize, btnSize, 0xFF1A1A1A);
        DrawRectClipped(bx, by, btnSize, btnSize, 0xFF2A2A2A);
        FillRectClipped(bx + 4, by + 4, btnSize - 8, btnSize - 8, 0xFFB04040);

        // Max
        bx -= (btnSize + 6);
        FillRectClipped(bx, by, btnSize, btnSize, 0xFF1A1A1A);
        DrawRectClipped(bx, by, btnSize, btnSize, 0xFF2A2A2A);
        FillRectClipped(bx + 4, by + 4, btnSize - 8, btnSize - 8, 0xFF4040B0);

        // Min
        bx -= (btnSize + 6);
        FillRectClipped(bx, by, btnSize, btnSize, 0xFF1A1A1A);
        DrawRectClipped(bx, by, btnSize, btnSize, 0xFF2A2A2A);
        FillRectClipped(bx + 4, by + btnSize - 6, btnSize - 8, 2, 0xFFEAEAEA);

        // Demo content area (very light)
        FillRectClipped(w.x + 10, w.y + titleH + 10, w.width - 20, w.height - titleH - 20, 0xFF161616);
    }

    // Cursor is drawn by Desktop last (so it stays above taskbar/menu).
}
