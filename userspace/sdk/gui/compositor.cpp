#include "compositor.h"
#include "../morphic_syscalls.h"
#include "../graphics_uapi.h"
#include "../font8x16.h"

static inline void *sys_video_map_direct() {
  uint64_t ret;
  __asm__ volatile("mov $50, %%rax\n\t"
                   "syscall\n\t"
                   : "=a"(ret)
                   :
                   : "rcx", "r11", "memory");
  return (void *)ret;
}

uint32_t *Compositor::frontBuffer = nullptr;
uint32_t *Compositor::backBuffer = nullptr;
uint32_t *Compositor::wallpaperBuffer = nullptr;
uint32_t *Compositor::currentBuffer = nullptr;
int Compositor::width = 0;
int Compositor::height = 0;
int Compositor::bpp = 32;
static Compositor::BackendType g_backend_requested = Compositor::BackendType::SOFTWARE;
static Compositor::BackendType g_backend_active = Compositor::BackendType::SOFTWARE;
static bool g_backend_gpu_atomic_supported = false;
static int  g_wallpaper_theme = 0; // 0=Aurora Cyan, 1=Midnight Violet, 2=Deep Obsidian

void Compositor::SetWallpaperTheme(int themeId) {
  g_wallpaper_theme = ((themeId % 3) + 3) % 3;
  BuildWallpaper();
}

int Compositor::GetWallpaperTheme() {
  return g_wallpaper_theme;
}

bool Compositor::clipEnabled = false;
int Compositor::clipX = 0;
int Compositor::clipY = 0;
int Compositor::clipW = 0;
int Compositor::clipH = 0;

static inline int i_max(int a, int b) { return a > b ? a : b; }
static inline int i_min(int a, int b) { return a < b ? a : b; }

static inline uint32_t BlendARGB(uint32_t dst, uint32_t src) {
  uint32_t a = (src >> 24) & 0xFFu;
  if (a == 255u) return src;
  if (a == 0u) return dst;
  uint32_t inv = 255u - a;
  uint32_t rb = (((src & 0x00FF00FFu) * a) + ((dst & 0x00FF00FFu) * inv) + 0x00800080u);
  rb = (rb + ((rb >> 8) & 0x00FF00FFu)) >> 8;
  uint32_t g = ((((src >> 8) & 0xFFu) * a) + (((dst >> 8) & 0xFFu) * inv) + 128u);
  g = (g + (g >> 8)) >> 8;
  return 0xFF000000u | (rb & 0x00FF00FFu) | ((g & 0xFFu) << 8);
}

static inline uint32_t LerpARGB(uint32_t c0, uint32_t c1, uint32_t t) {
  if (t == 0u) return c0;
  if (t >= 255u) return c1;
  uint32_t inv = 255u - t;
  uint32_t a = ((((c0 >> 24) & 0xFFu) * inv) + (((c1 >> 24) & 0xFFu) * t)) / 255u;
  uint32_t r = ((((c0 >> 16) & 0xFFu) * inv) + (((c1 >> 16) & 0xFFu) * t)) / 255u;
  uint32_t g = ((((c0 >> 8) & 0xFFu) * inv) + (((c1 >> 8) & 0xFFu) * t)) / 255u;
  uint32_t b = (((c0 & 0xFFu) * inv) + ((c1 & 0xFFu) * t)) / 255u;
  return (a << 24) | (r << 16) | (g << 8) | b;
}

static inline uint32_t WithAlphaARGB(uint32_t c, uint8_t a) {
  return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

bool Compositor::SetBackend(BackendType backend) {
  if (backend != BackendType::SOFTWARE &&
      backend != BackendType::GPU_EXPERIMENTAL) {
    return false;
  }
  g_backend_requested = backend;
  g_backend_active = backend;
  return true;
}

Compositor::BackendType Compositor::GetBackend() { return g_backend_active; }

bool Compositor::IsBackendAccelerated() {
  return g_backend_active == BackendType::GPU_EXPERIMENTAL;
}

const char* Compositor::GetBackendName() {
  if (g_backend_active == BackendType::GPU_EXPERIMENTAL) {
    return "backend_gpu_experimental";
  }
  if (g_backend_requested == BackendType::GPU_EXPERIMENTAL &&
      g_backend_active == BackendType::SOFTWARE) {
    return "backend_sw_fallback";
  }
  return "backend_sw";
}

// Pre-render a sleek Glass Dark Aurora Mesh Wallpaper once at startup
void Compositor::BuildWallpaper() {
  if (!wallpaperBuffer || width <= 0 || height <= 0) return;

  // Radial aurora centers
  int ax1 = (width * 22) / 100;
  int ay1 = (height * 26) / 100;
  int r1 = (width * 48) / 100;
  int64_t r1_2 = (int64_t)r1 * r1;

  int ax2 = (width * 80) / 100;
  int ay2 = (height * 74) / 100;
  int r2 = (width * 46) / 100;
  int64_t r2_2 = (int64_t)r2 * r2;

  int ax3 = (width * 56) / 100;
  int ay3 = (height * 18) / 100;
  int r3 = (width * 34) / 100;
  int64_t r3_2 = (int64_t)r3 * r3;

  for (int y = 0; y < height; y++) {
    uint32_t ty = (uint32_t)((y * 255) / (height > 1 ? height - 1 : 1));
    // Deep obsidian-slate vertical base
    uint32_t baseR = 9 + (ty * 4) / 255;
    uint32_t baseG = 13 + (ty * 7) / 255;
    uint32_t baseB = 24 + (ty * 14) / 255;

    int64_t dy1 = (int64_t)(y - ay1);
    int64_t dy1_2 = dy1 * dy1;
    int64_t dy2 = (int64_t)(y - ay2);
    int64_t dy2_2 = dy2 * dy2;
    int64_t dy3 = (int64_t)(y - ay3);
    int64_t dy3_2 = dy3 * dy3;

    uint32_t *row = &wallpaperBuffer[(int64_t)y * width];
    bool gridY = ((y & 31) == 0);

    for (int x = 0; x < width; x++) {
      uint32_t r = baseR;
      uint32_t g = baseG;
      uint32_t b = baseB;

      // Aurora 1 glow (theme-selectable)
      int64_t dx1 = (int64_t)(x - ax1);
      int64_t d1 = dx1 * dx1 + dy1_2;
      if (d1 < r1_2) {
        uint32_t w1 = (uint32_t)(((r1_2 - d1) * 255) / r1_2);
        w1 = (w1 * w1) >> 8;
        if (g_wallpaper_theme == 0) { r += (w1 * 12) >> 8; g += (w1 * 74) >> 8; b += (w1 * 116) >> 8; }
        else if (g_wallpaper_theme == 1) { r += (w1 * 88) >> 8; g += (w1 * 28) >> 8; b += (w1 * 124) >> 8; }
        else { r += (w1 * 18) >> 8; g += (w1 * 24) >> 8; b += (w1 * 34) >> 8; }
      }

      // Aurora 2 glow (theme-selectable)
      int64_t dx2 = (int64_t)(x - ax2);
      int64_t d2 = dx2 * dx2 + dy2_2;
      if (d2 < r2_2) {
        uint32_t w2 = (uint32_t)(((r2_2 - d2) * 255) / r2_2);
        w2 = (w2 * w2) >> 8;
        if (g_wallpaper_theme == 0) { r += (w2 * 58) >> 8; g += (w2 * 38) >> 8; b += (w2 * 118) >> 8; }
        else if (g_wallpaper_theme == 1) { r += (w2 * 120) >> 8; g += (w2 * 24) >> 8; b += (w2 * 82) >> 8; }
        else { r += (w2 * 14) >> 8; g += (w2 * 20) >> 8; b += (w2 * 30) >> 8; }
      }

      // Aurora 3: Soft Teal highlight (upper-center)
      int64_t dx3 = (int64_t)(x - ax3);
      int64_t d3 = dx3 * dx3 + dy3_2;
      if (d3 < r3_2) {
        uint32_t w3 = (uint32_t)(((r3_2 - d3) * 255) / r3_2);
        w3 = (w3 * w3) >> 8;
        r += (w3 * 10) >> 8;
        g += (w3 * 42) >> 8;
        b += (w3 * 64) >> 8;
      }

      // Subtle architectural micro-dot matrix
      if (gridY && ((x & 31) == 0)) {
        r += 10;
        g += 14;
        b += 20;
      }

      if (r > 255) r = 255;
      if (g > 255) g = 255;
      if (b > 255) b = 255;
      row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
  }
}

void Compositor::BlitWallpaperClipped() {
  if (!currentBuffer) return;
  if (!wallpaperBuffer) {
    FillRectClipped(0, 0, width, height, 0xFF0B101B);
    return;
  }

  int x = 0, y = 0, w = width, h = height;
  if (clipEnabled) {
    x = i_max(0, clipX);
    y = i_max(0, clipY);
    int x2 = i_min(width, clipX + clipW);
    int y2 = i_min(height, clipY + clipH);
    w = x2 - x;
    h = y2 - y;
    if (w <= 0 || h <= 0) return;
  }

  for (int row = 0; row < h; row++) {
    int64_t off = (int64_t)(y + row) * width + x;
    uint32_t *src = &wallpaperBuffer[off];
    uint32_t *dst = &currentBuffer[off];
    bool aligned = (((uintptr_t)src & 7) == 0) && (((uintptr_t)dst & 7) == 0);
    if (aligned && w >= 2) {
      uint64_t *s64 = (uint64_t *)src;
      uint64_t *d64 = (uint64_t *)dst;
      int pairs = w / 2;
      for (int i = 0; i < pairs; i++) d64[i] = s64[i];
      if (w & 1) dst[w - 1] = src[w - 1];
    } else {
      for (int i = 0; i < w; i++) dst[i] = src[i];
    }
  }
}

bool Compositor::Initialize() {
  uint64_t info = sys_get_screen_info();
  auto debug_print_ptr_raw = [](const char *label, uint64_t v) {
    char tmp[32];
    const char *hex = "0123456789ABCDEF";
    tmp[0] = '0'; tmp[1] = 'x';
    for (int i = 0; i < 16; i++) tmp[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xF];
    tmp[18] = '\0';
    sys_debug_print(label);
    sys_debug_print(tmp);
    sys_debug_print("\n");
  };
  debug_print_ptr_raw("[Compositor] sys_get_screen_info=", info);

  width = (info >> 32) & 0xFFFFFFFF;
  height = info & 0xFFFFFFFF;
  if (width == 0 || height == 0) return false;

  sys_debug_print("[Compositor] about to call sys_video_map\n");
  frontBuffer = (uint32_t *)sys_video_map();
  if (!frontBuffer) {
    frontBuffer = (uint32_t *)sys_video_map_direct();
  }
  if (!frontBuffer) return false;

  const uint64_t MAX_PIXELS = 3840 * 2160;
  const uint64_t BUF_SIZE = MAX_PIXELS * 4;
  uint64_t addr = sys_alloc_backbuffer(BUF_SIZE);
  if (addr && (uint64_t)width * height <= MAX_PIXELS) {
    backBuffer = (uint32_t *)addr;
  } else {
    backBuffer = frontBuffer;
  }

  // Allocate dedicated wallpaper cache (fast 64-bit blit per dirty rect)
  uint64_t wpSize = (uint64_t)width * (uint64_t)height * 4;
  wallpaperBuffer = (uint32_t *)sys_malloc(wpSize);
  if (wallpaperBuffer) {
    BuildWallpaper();
  }

  currentBuffer = backBuffer;

  g_backend_active = g_backend_requested;
  g_backend_gpu_atomic_supported = false;

  if (g_backend_requested == BackendType::GPU_EXPERIMENTAL) {
    GraphicsUapiCaps caps = {};
    if (MorphicGfx::QueryCaps(&caps) &&
        (caps.caps_flags & GRAPHICS_CAP_ATOMIC_COMMIT) != 0) {
      g_backend_gpu_atomic_supported = true;
      sys_debug_print("[Compositor] backend_gpu_experimental enabled\n");
    } else {
      g_backend_active = BackendType::SOFTWARE;
      sys_debug_print("[Compositor] backend_gpu_experimental unavailable, fallback to backend_sw\n");
    }
  }

  sys_debug_print("[Compositor] Init: v3.0 (Glass Dark UI)\n");
  return true;
}

void Compositor::SetRenderTarget(RenderTarget target) {
  if (target == RenderTarget::BACK_BUFFER) currentBuffer = backBuffer;
  else if (target == RenderTarget::FRONT_BUFFER) currentBuffer = frontBuffer;
}

void Compositor::SetClip(int x, int y, int w, int h) {
  clipEnabled = true;
  clipX = x; clipY = y; clipW = w; clipH = h;
}

void Compositor::ClearClip() { clipEnabled = false; }

bool Compositor::IntersectsClip(int x, int y, int w, int h) {
  if (!clipEnabled) return true;
  return !(x + w <= clipX || x >= clipX + clipW || y + h <= clipY || y >= clipY + clipH);
}

void Compositor::BlendPixelClipped(int x, int y, uint32_t argb) {
  if (!currentBuffer) return;
  if (x < 0 || x >= width || y < 0 || y >= height) return;
  if (clipEnabled) {
    if (x < clipX || x >= clipX + clipW || y < clipY || y >= clipY + clipH) return;
  }
  uint32_t &dst = currentBuffer[(int64_t)y * width + x];
  dst = BlendARGB(dst, argb);
}

void Compositor::FillRectClipped(int x, int y, int w, int h, uint32_t color) {
  if (!currentBuffer || w <= 0 || h <= 0) return;

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width) w = width - x;
  if (y + h > height) h = height - y;
  if (w <= 0 || h <= 0) return;

  if (clipEnabled) {
    int nx = i_max(x, clipX);
    int ny = i_max(y, clipY);
    int nx2 = i_min(x + w, clipX + clipW);
    int ny2 = i_min(y + h, clipY + clipH);
    x = nx; y = ny; w = nx2 - nx; h = ny2 - ny;
    if (w <= 0 || h <= 0) return;
  }

  uint32_t alpha = (color >> 24) & 0xFFu;
  if (alpha == 0u) return;

  if (alpha == 255u) {
    for (int j = 0; j < h; j++) {
      int64_t offset = (int64_t)(y + j) * width + x;
      uint32_t *row = &currentBuffer[offset];
      if (w >= 4 && ((uintptr_t)row & 7) == 0) {
        uint64_t color64 = ((uint64_t)color << 32) | color;
        uint64_t *row64 = (uint64_t *)row;
        int pairs = w / 2;
        for (int i = 0; i < pairs; i++) row64[i] = color64;
        if (w & 1) row[w - 1] = color;
      } else {
        for (int i = 0; i < w; i++) row[i] = color;
      }
    }
  } else {
    for (int j = 0; j < h; j++) {
      uint32_t *row = &currentBuffer[(int64_t)(y + j) * width + x];
      for (int i = 0; i < w; i++) {
        row[i] = BlendARGB(row[i], color);
      }
    }
  }
}

void Compositor::DrawRectClipped(int x, int y, int w, int h, uint32_t color) {
  FillRectClipped(x, y, w, 1, color);
  FillRectClipped(x, y + h - 1, w, 1, color);
  FillRectClipped(x, y, 1, h, color);
  FillRectClipped(x + w - 1, y, 1, h, color);
}

void Compositor::DrawRect(int x, int y, int w, int h, uint32_t color) {
  FillRectClipped(x, y, w, h, color);
}

void Compositor::DrawRectAlpha(int x, int y, int w, int h, uint32_t argb) {
  FillRectClipped(x, y, w, h, argb);
}

void Compositor::DrawGradientV(int x, int y, int w, int h, uint32_t topColor, uint32_t bottomColor) {
  if (w <= 0 || h <= 0 || !IntersectsClip(x, y, w, h)) return;
  int denom = h > 1 ? (h - 1) : 1;
  for (int j = 0; j < h; j++) {
    uint32_t t = (uint32_t)((j * 255) / denom);
    FillRectClipped(x, y + j, w, 1, LerpARGB(topColor, bottomColor, t));
  }
}

void Compositor::DrawRoundedRect(int x, int y, int w, int h, int radius, uint32_t argb) {
  if (w <= 0 || h <= 0 || !IntersectsClip(x, y, w, h)) return;
  if (radius <= 0) { FillRectClipped(x, y, w, h, argb); return; }
  int maxR = i_min(w, h) / 2;
  if (radius > maxR) radius = maxR;

  uint32_t baseA = (argb >> 24) & 0xFFu;
  if (baseA == 0u) return;

  if (h > 2 * radius) {
    FillRectClipped(x, y + radius, w, h - 2 * radius, argb);
  }

  int r2 = radius * radius * 16;
  for (int row = 0; row < radius; row++) {
    int yTop = y + row;
    int yBot = y + h - 1 - row;
    if (w > 2 * radius) {
      FillRectClipped(x + radius, yTop, w - 2 * radius, 1, argb);
      FillRectClipped(x + radius, yBot, w - 2 * radius, 1, argb);
    }
    for (int col = 0; col < radius; col++) {
      int cov = 0;
      for (int sy = 0; sy < 4; sy++) {
        int dy = (radius - row) * 4 - sy - 1;
        for (int sx = 0; sx < 4; sx++) {
          int dx = (radius - col) * 4 - sx - 1;
          if (dx * dx + dy * dy <= r2) cov++;
        }
      }
      if (cov == 0) continue;
      uint32_t px = WithAlphaARGB(argb, (uint8_t)((baseA * (uint32_t)cov) >> 4));
      BlendPixelClipped(x + col, yTop, px);
      BlendPixelClipped(x + w - 1 - col, yTop, px);
      BlendPixelClipped(x + col, yBot, px);
      BlendPixelClipped(x + w - 1 - col, yBot, px);
    }
  }
}

void Compositor::DrawRoundedGradientV(int x, int y, int w, int h, int radius, uint32_t topColor, uint32_t bottomColor) {
  if (w <= 0 || h <= 0 || !IntersectsClip(x, y, w, h)) return;
  if (radius <= 0) { DrawGradientV(x, y, w, h, topColor, bottomColor); return; }
  int maxR = i_min(w, h) / 2;
  if (radius > maxR) radius = maxR;

  int denom = h > 1 ? (h - 1) : 1;
  int r2 = radius * radius * 16;

  for (int j = 0; j < h; j++) {
    uint32_t rowCol = LerpARGB(topColor, bottomColor, (uint32_t)((j * 255) / denom));
    uint32_t baseA = (rowCol >> 24) & 0xFFu;
    if (baseA == 0u) continue;

    int rowCorner = -1;
    if (j < radius) rowCorner = j;
    else if (j >= h - radius) rowCorner = h - 1 - j;

    if (rowCorner < 0) {
      FillRectClipped(x, y + j, w, 1, rowCol);
    } else {
      if (w > 2 * radius) {
        FillRectClipped(x + radius, y + j, w - 2 * radius, 1, rowCol);
      }
      for (int col = 0; col < radius; col++) {
        int cov = 0;
        for (int sy = 0; sy < 4; sy++) {
          int dy = (radius - rowCorner) * 4 - sy - 1;
          for (int sx = 0; sx < 4; sx++) {
            int dx = (radius - col) * 4 - sx - 1;
            if (dx * dx + dy * dy <= r2) cov++;
          }
        }
        if (cov == 0) continue;
        uint32_t px = WithAlphaARGB(rowCol, (uint8_t)((baseA * (uint32_t)cov) >> 4));
        BlendPixelClipped(x + col, y + j, px);
        BlendPixelClipped(x + w - 1 - col, y + j, px);
      }
    }
  }
}

void Compositor::DrawRoundedBorder(int x, int y, int w, int h, int radius, uint32_t argb) {
  if (w <= 1 || h <= 1 || !IntersectsClip(x, y, w, h)) return;
  if (radius <= 0) { DrawRectClipped(x, y, w, h, argb); return; }
  int maxR = i_min(w, h) / 2;
  if (radius > maxR) radius = maxR;

  FillRectClipped(x + radius, y, w - 2 * radius, 1, argb);
  FillRectClipped(x + radius, y + h - 1, w - 2 * radius, 1, argb);
  FillRectClipped(x, y + radius, 1, h - 2 * radius, argb);
  FillRectClipped(x + w - 1, y + radius, 1, h - 2 * radius, argb);

  uint32_t baseA = (argb >> 24) & 0xFFu;
  int rOut2 = radius * radius * 16;
  int rIn = radius > 1 ? (radius - 1) : 0;
  int rIn2 = rIn * rIn * 16;

  for (int row = 0; row < radius; row++) {
    for (int col = 0; col < radius; col++) {
      int ring = 0;
      for (int sy = 0; sy < 4; sy++) {
        int dy = (radius - row) * 4 - sy - 1;
        for (int sx = 0; sx < 4; sx++) {
          int dx = (radius - col) * 4 - sx - 1;
          int d2 = dx * dx + dy * dy;
          if (d2 <= rOut2 && d2 >= rIn2) ring++;
        }
      }
      if (ring == 0) continue;
      uint32_t px = WithAlphaARGB(argb, (uint8_t)((baseA * (uint32_t)ring) >> 4));
      BlendPixelClipped(x + col, y + row, px);
      BlendPixelClipped(x + w - 1 - col, y + row, px);
      BlendPixelClipped(x + col, y + h - 1 - row, px);
      BlendPixelClipped(x + w - 1 - col, y + h - 1 - row, px);
    }
  }
}

void Compositor::DrawSoftShadow(int x, int y, int w, int h, int radius, int spread, uint8_t maxAlpha) {
  if (spread <= 0 || maxAlpha == 0) return;
  if (!IntersectsClip(x - spread, y - spread, w + spread * 2, h + spread * 2)) return;
  const int layers = 4;
  for (int i = layers; i >= 1; i--) {
    int pad = (i * spread) / layers;
    uint8_t a = (uint8_t)((maxAlpha * (layers - i + 1)) / (layers * 2));
    DrawRoundedRect(x - pad / 2, y + pad / 2 + 2, w + pad, h + pad, radius + pad / 2,
                    ((uint32_t)a << 24));
  }
}

void Compositor::DrawCircle(int cx, int cy, int r, uint32_t argb) {
  if (r <= 0) return;
  DrawRoundedRect(cx - r, cy - r, r * 2, r * 2, r, argb);
}

// Modern 16x22 Crisp Pointer Cursor with Soft Drop Shadow
// 0=Trans, 1=White fill, 2=Dark outline, 3=Soft shadow
static const uint8_t cursor_w = 16;
static const uint8_t cursor_h = 22;
static const uint8_t cursor_bitmap[22 * 16] = {
    2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,2,3,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,2,3,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,2,3,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,2,3,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,2,3,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,3,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,2,3,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,2,3,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,2,3,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,1,2,3,0,0,0,
    2,1,1,1,1,1,1,2,2,2,2,2,2,3,0,0,
    2,1,1,1,2,1,1,2,3,3,3,3,3,0,0,0,
    2,1,1,2,0,2,1,1,2,3,0,0,0,0,0,0,
    2,1,2,3,0,2,1,1,2,3,0,0,0,0,0,0,
    2,2,3,0,0,0,2,1,1,2,3,0,0,0,0,0,
    2,3,0,0,0,0,2,1,1,2,3,0,0,0,0,0,
    0,0,0,0,0,0,0,2,1,1,2,3,0,0,0,0,
    0,0,0,0,0,0,0,2,2,2,2,3,0,0,0,0,
    0,0,0,0,0,0,0,0,3,3,3,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void Compositor::DrawCursor(int x, int y) { DrawCursorClipped(x, y); }

void Compositor::DrawCursorClipped(int x, int y) {
  if (!currentBuffer) return;
  for (int cy = 0; cy < cursor_h; cy++) {
    for (int cx = 0; cx < cursor_w; cx++) {
      uint8_t code = cursor_bitmap[cy * cursor_w + cx];
      if (code == 0) continue;
      uint32_t col = (code == 1) ? 0xFFF8FAFC : (code == 2 ? 0xFF090D16 : 0x55000000);
      BlendPixelClipped(x + cx, y + cy, col);
    }
  }
}

void Compositor::DrawCursorToFront(int x, int y) {
  if (!frontBuffer) return;
  for (int cy = 0; cy < cursor_h; cy++) {
    int py = y + cy;
    if (py < 0 || py >= height) continue;
    for (int cx = 0; cx < cursor_w; cx++) {
      int px = x + cx;
      if (px < 0 || px >= width) continue;
      uint8_t code = cursor_bitmap[cy * cursor_w + cx];
      if (code == 0) continue;
      uint32_t col = (code == 1) ? 0xFFF8FAFC : (code == 2 ? 0xFF090D16 : 0x55000000);
      uint32_t &dst = frontBuffer[(int64_t)py * width + px];
      dst = (code == 3) ? BlendARGB(dst, col) : col;
    }
  }
}

void Compositor::Clear(uint32_t color) {
  if (!currentBuffer) return;
  uint64_t color64 = ((uint64_t)color << 32) | color;
  uint64_t *dst = (uint64_t *)currentBuffer;
  uint64_t count = ((uint64_t)width * height) / 2;
  for (uint64_t i = 0; i < count; i++) dst[i] = color64;
  if ((width * height) & 1) currentBuffer[width * height - 1] = color;
}

bool Compositor::SwapBuffers() {
  Flush();
  return Present();
}

bool Compositor::SwapBuffersRect(int x, int y, int w, int h) {
  FlushRect(x, y, w, h);
  if (!frontBuffer) return false;

  if (g_backend_active == BackendType::GPU_EXPERIMENTAL && g_backend_gpu_atomic_supported) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return MorphicGfx::AtomicCommitFull(true) != 0;
    bool committed = MorphicGfx::AtomicCommit((int16_t)x, (int16_t)y,
                                              (uint16_t)w, (uint16_t)h,
                                              MorphicGfx::ATOMIC_WAIT_VSYNC) != 0;
    if (!committed) committed = MorphicGfx::AtomicCommitFull(true) != 0;
    return committed;
  }

  uint64_t xy = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
  uint64_t wh = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
  return sys_video_flip_rect(frontBuffer, xy, wh) != 0;
}

void Compositor::Flush() {
  if (!backBuffer || !frontBuffer || backBuffer == frontBuffer) return;
  uint64_t *src = (uint64_t *)backBuffer;
  uint64_t *dst = (uint64_t *)frontBuffer;
  uint64_t count = ((uint64_t)width * height) / 2;
  for (uint64_t i = 0; i < count; i++) dst[i] = src[i];
  if ((width * height) & 1) frontBuffer[width * height - 1] = backBuffer[width * height - 1];
}

void Compositor::FlushRect(int x, int y, int w, int h) {
  if (!backBuffer || !frontBuffer || backBuffer == frontBuffer) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > width) w = width - x;
  if (y + h > height) h = height - y;
  if (w <= 0 || h <= 0) return;

  for (int row = 0; row < h; row++) {
    uint32_t *srcRow = &backBuffer[(int64_t)(y + row) * width + x];
    uint32_t *dstRow = &frontBuffer[(int64_t)(y + row) * width + x];
    bool aligned = ((uintptr_t)srcRow & 7) == 0 && ((uintptr_t)dstRow & 7) == 0;
    if (aligned && w >= 2) {
      uint64_t *src64 = (uint64_t *)srcRow;
      uint64_t *dst64 = (uint64_t *)dstRow;
      int pairs = w / 2;
      for (int i = 0; i < pairs; i++) dst64[i] = src64[i];
      if (w & 1) dstRow[w - 1] = srcRow[w - 1];
    } else {
      for (int col = 0; col < w; col++) dstRow[col] = srcRow[col];
    }
  }
}

bool Compositor::Present() {
  if (!frontBuffer) return false;
  if (g_backend_active == BackendType::GPU_EXPERIMENTAL && g_backend_gpu_atomic_supported) {
    return MorphicGfx::AtomicCommitFull(true) != 0;
  }
  return sys_video_flip(frontBuffer) != 0;
}

bool Compositor::PresentFullDirty() {
  if (!frontBuffer) return false;
  if (g_backend_active == BackendType::GPU_EXPERIMENTAL && g_backend_gpu_atomic_supported) {
    return MorphicGfx::AtomicCommitFull(true) != 0;
  }
  return sys_video_flip_flags(frontBuffer, 1) != 0;
}

int Compositor::MeasureText(const char *text, int scale) {
  if (!text) return 0;
  if (scale < 1) scale = 1;
  int len = 0;
  while (text[len]) len++;
  return len * 8 * scale;
}

void Compositor::DrawChar(int x, int y, char ch, uint32_t color, int scale) {
  if (!currentBuffer) return;
  if (scale < 1) scale = 1;
  uint8_t c = (uint8_t)ch;
  const uint8_t *glyph = &font8x16[c * 16];

  for (int row = 0; row < 16; row++) {
    uint8_t bits = glyph[row];
    uint8_t prevBits = (row > 0) ? glyph[row - 1] : 0;
    uint8_t nextBits = (row < 15) ? glyph[row + 1] : 0;
    for (int col = 0; col < 8; col++) {
      bool on = (bits & (0x80 >> col)) != 0;
      if (scale == 1) {
        if (!on) continue;
        BlendPixelClipped(x + col, y + row, color);
      } else if (on) {
        FillRectClipped(x + col * scale, y + row * scale, scale, scale, color);
      } else if (scale == 2) {
        // Subtle 2x anti-aliased corner rounding for smooth large headers & monograms
        bool up    = (prevBits & (0x80 >> col)) != 0;
        bool down  = (nextBits & (0x80 >> col)) != 0;
        bool left  = (col > 0) && ((bits & (0x80 >> (col - 1))) != 0);
        bool right = (col < 7) && ((bits & (0x80 >> (col + 1))) != 0);
        uint32_t aaCol = WithAlphaARGB(color, 0x58);
        int bx = x + col * 2;
        int by = y + row * 2;
        if (up && left)    BlendPixelClipped(bx,     by,     aaCol);
        if (up && right)   BlendPixelClipped(bx + 1, by,     aaCol);
        if (down && left)  BlendPixelClipped(bx,     by + 1, aaCol);
        if (down && right) BlendPixelClipped(bx + 1, by + 1, aaCol);
      }
    }
  }
}

void Compositor::DrawText(int x, int y, const char *text, uint32_t color, int scale) {
  if (!text) return;
  int cx = x;
  int startX = x;
  for (const char *p = text; *p; p++) {
    char ch = *p;
    if (ch == '\n') {
      y += (16 * scale);
      cx = startX;
      continue;
    }
    DrawChar(cx, y, ch, color, scale);
    cx += 8 * scale;
  }
}

void Compositor::DrawClockText(int rightX, int y, uint32_t hh, uint32_t mm,
                               uint32_t ss, uint32_t dd, uint32_t mo) {
  char buf[32];
  buf[0] = (char)('0' + (hh / 10) % 10);
  buf[1] = (char)('0' + (hh % 10));
  buf[2] = ':';
  buf[3] = (char)('0' + (mm / 10) % 10);
  buf[4] = (char)('0' + (mm % 10));
  buf[5] = ':';
  buf[6] = (char)('0' + (ss / 10) % 10);
  buf[7] = (char)('0' + (ss % 10));
  buf[8] = ' ';
  buf[9] = '|';
  buf[10] = ' ';
  buf[11] = (char)('0' + (dd / 10) % 10);
  buf[12] = (char)('0' + (dd % 10));
  buf[13] = '/';
  buf[14] = (char)('0' + (mo / 10) % 10);
  buf[15] = (char)('0' + (mo % 10));
  buf[16] = '\0';

  // Fixed exact 8px-per-glyph measurement so clock never clips off-screen
  int w = 16 * 8;
  DrawText(rightX - w, y, buf, 0xFFF1F5F9, 1);
}

void Compositor::RenderMenu(bool menuOpen) {
  (void)menuOpen;
}

void Compositor::RenderTaskbar(Window *windows, int windowCount,
                               void *extWindows, int extWindowCount,
                               bool menuOpen, const MorphicDateTime &dt) {
  // 1. Sleek 28px Glass Dark Top Status Bar (macOS / Hyprland inspired)
  DrawRectAlpha(0, 0, width, 28, 0xCC0A0F1C);
  FillRectClipped(0, 27, width, 1, 0x24FFFFFF);
  DrawRoundedRect(12, 5, 108, 18, 9, 0x2A38BDF8);
  DrawText(22, 6, "MORPHIC OS", 0xFF38BDF8, 1);
  DrawText(width - 148, 6, "VK 1.3  GLASS", 0xFF94A3B8, 1);

  // 2. Floating Translucent Glass Dock at bottom
  const int dockH = 44;
  const int dockMarginX = 16;
  const int dockY = height - dockH - 8;
  const int dockW = width - (dockMarginX * 2);

  DrawSoftShadow(dockMarginX, dockY, dockW, dockH, 14, 10, 90);
  DrawRoundedRect(dockMarginX, dockY, dockW, dockH, 14, 0xDC0E1627);
  DrawRoundedBorder(dockMarginX, dockY, dockW, dockH, 14, 0x30FFFFFF);

  // Launcher ("Apps") glowing pill button
  int btnX = dockMarginX + 10;
  int btnY = dockY + 6;
  int btnW = 92;
  int btnH = 32;
  if (menuOpen) {
    DrawRoundedGradientV(btnX, btnY, btnW, btnH, 10, 0xFF38BDF8, 0xFF0284C7);
    DrawText(btnX + 22, btnY + 8, "Apps", 0xFF06101C, 1);
  } else {
    DrawRoundedGradientV(btnX, btnY, btnW, btnH, 10, 0xFF1D2A42, 0xFF131C2E);
    DrawRoundedBorder(btnX, btnY, btnW, btnH, 10, 0x4438BDF8);
    DrawCircle(btnX + 16, btnY + 16, 4, 0xFF38BDF8);
    DrawText(btnX + 28, btnY + 8, "Apps", 0xFFF1F5F9, 1);
  }

  // Subtle vertical separator
  FillRectClipped(btnX + btnW + 10, dockY + 10, 1, dockH - 20, 0x26FFFFFF);

  // Internal Window Pills
  int iconX = btnX + btnW + 20;
  for (int i = 0; i < windowCount; i++) {
    bool active = !windows[i].minimized;
    uint32_t pillBg = active ? 0xFF1F2D45 : 0xFF141D2E;
    uint32_t pillBorder = active ? 0x5538BDF8 : 0x22FFFFFF;
    DrawRoundedRect(iconX, btnY, 32, 32, 9, pillBg);
    DrawRoundedBorder(iconX, btnY, 32, 32, 9, pillBorder);

    uint32_t dotCol = (windows[i].color & 0x00FFFFFFu) | 0xFF000000u;
    DrawRoundedRect(iconX + 8, btnY + 8, 16, 16, 5, dotCol);
    if (active) {
      DrawRoundedRect(iconX + 10, dockY + dockH - 4, 12, 3, 1, 0xFF38BDF8);
    }
    iconX += 40;
  }

  // External App Window Pills
  if (extWindows && extWindowCount > 0) {
    WindowInfo *ext = (WindowInfo *)extWindows;
    for (int i = 0; i < extWindowCount; i++) {
      if (ext[i].flags == 0) continue;
      uint32_t accent = 0xFF38BDF8;
      char initial = 'A';
      if (ext[i].title[0]) initial = ext[i].title[0];
      if (initial == 'C') accent = 0xFF34D399;
      else if (initial == 'T') accent = 0xFF818CF8;
      else if (initial == 'F') accent = 0xFFFBBF24;
      else if (initial == 'I') accent = 0xFFF472B6;

      DrawRoundedRect(iconX, btnY, 32, 32, 9, 0xFF1E2B42);
      DrawRoundedBorder(iconX, btnY, 32, 32, 9, 0x44FFFFFF);
      DrawRoundedRect(iconX + 6, btnY + 6, 20, 20, 6, accent);
      char chStr[2] = {initial, '\0'};
      DrawText(iconX + 12, btnY + 8, chStr, 0xFF06101C, 1);
      DrawRoundedRect(iconX + 10, dockY + dockH - 4, 12, 3, 1, accent);
      iconX += 40;
    }
  }

  // Right-side Clock & Date Pill (never clipped)
  int clockPillW = 156;
  int clockPillX = dockMarginX + dockW - clockPillW - 10;
  DrawRoundedRect(clockPillX, btnY, clockPillW, 32, 10, 0xFF141D2E);
  DrawRoundedBorder(clockPillX, btnY, clockPillW, 32, 10, 0x24FFFFFF);

  uint32_t hh = dt.hour;
  uint32_t mm = dt.minute;
  uint32_t ss = dt.second;
  uint32_t dd = dt.day > 0 ? dt.day : 1;
  uint32_t mo = dt.month > 0 ? dt.month : 1;
  DrawClockText(clockPillX + clockPillW - 14, btnY + 8, hh, mm, ss, dd, mo);
}

void Compositor::RenderScene(Window *windows, int windowCount, int mouseX, int mouseY) {
  (void)mouseX;
  (void)mouseY;

  // 1. Aurora Glass Dark Wallpaper (instant 64-bit blit per dirty rect)
  BlitWallpaperClipped();

  // 2. Glass Dark Windows
  const int titleH = 30;
  const int radius = 12;

  for (int i = 0; i < windowCount; i++) {
    Window &w = windows[i];
    if (w.minimized) continue;
    if (!IntersectsClip(w.x - 16, w.y - 16, w.width + 32, w.height + 36)) continue;

    // Soft ambient shadow
    DrawSoftShadow(w.x, w.y, w.width, w.height, radius, 14, 95);

    // Translucent frosted glass body
    DrawRoundedRect(w.x, w.y, w.width, w.height, radius, 0xEE111827);

    // Titlebar band (top rounded gradient)
    DrawRoundedGradientV(w.x, w.y, w.width, titleH, radius, 0xFF1B2740, 0xFF141D30);
    // Square off bottom of titlebar cleanly
    FillRectClipped(w.x, w.y + titleH - radius, w.width, radius, 0xFF141D30);

    // Subtle electric cyan hairline under titlebar
    FillRectClipped(w.x + 1, w.y + titleH - 1, w.width - 2, 1, 0x4438BDF8);

    // Window title + accent dot
    DrawCircle(w.x + 16, w.y + titleH / 2, 4, 0xFF38BDF8);
    if (w.title) {
      DrawText(w.x + 28, w.y + 7, w.title, 0xFFF1F5F9, 1);
    }

    // Modern circular traffic-light controls on right (aligned with desktop.cpp hit-test)
    int btnSize = 14;
    int pad = 8;
    int bx = w.x + w.width - pad - btnSize;
    int by = w.y + (titleH - btnSize) / 2;

    // Close (Rose)
    DrawCircle(bx + btnSize / 2, by + btnSize / 2, 6, 0xFFFB7185);
    // Maximize (Emerald)
    bx -= (btnSize + 6);
    DrawCircle(bx + btnSize / 2, by + btnSize / 2, 6, 0xFF34D399);
    // Minimize (Amber)
    bx -= (btnSize + 6);
    DrawCircle(bx + btnSize / 2, by + btnSize / 2, 6, 0xFFFBBF24);

    // Crisp outer glass rim
    DrawRoundedBorder(w.x, w.y, w.width, w.height, radius, 0x33FFFFFF);
  }
}
