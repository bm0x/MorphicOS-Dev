#include "vulkan_layer.h"
#include "graphics.h"
#include "../drm/drm.h"
#include "../arch/x86_64/pci.h"
#include "../serial/uart.h"
#include "../../utils/std.h"

extern "C" {
    void blit_fast_32(void* dest, void* src, size_t count);
    void memset_fast_32(void* dest, uint32_t value, size_t count);
}

namespace VulkanLayer {

    static MorphicVkPhysicalDeviceInfo s_devInfo = {};
    static volatile uint64_t s_batches = 0;
    static volatile uint64_t s_commands = 0;
    static bool s_initialized = false;

    static void CopyText(char* dst, size_t maxLen, const char* src) {
        size_t i = 0;
        if (src) {
            while (src[i] && i < maxLen - 1) {
                dst[i] = src[i];
                i++;
            }
        }
        dst[i] = '\0';
    }

    static inline int32_t Min3(int32_t a, int32_t b, int32_t c) {
        int32_t m = (a < b) ? a : b;
        return (m < c) ? m : c;
    }

    static inline int32_t Max3(int32_t a, int32_t b, int32_t c) {
        int32_t m = (a > b) ? a : b;
        return (m > c) ? m : c;
    }

    static inline uint32_t BlendARGB(uint32_t dst, uint32_t src) {
        uint32_t a = (src >> 24) & 0xFF;
        if (a == 255) return src;
        if (a == 0) return dst;
        uint32_t inv = 255 - a;
        uint32_t r = ((((src >> 16) & 0xFF) * a) + (((dst >> 16) & 0xFF) * inv)) >> 8;
        uint32_t g = ((((src >> 8) & 0xFF) * a) + (((dst >> 8) & 0xFF) * inv)) >> 8;
        uint32_t b = (((src & 0xFF) * a) + ((dst & 0xFF) * inv)) >> 8;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    void Init() {
        kmemset(&s_devInfo, 0, sizeof(s_devInfo));
        s_devInfo.api_version = MORPHIC_VK_API_VERSION_1_3;
        s_devInfo.driver_version = MORPHIC_VK_MAKE_VERSION(0, 6, 1);
        s_devInfo.queue_flags = MVK_QUEUE_GRAPHICS_BIT | MVK_QUEUE_COMPUTE_BIT | MVK_QUEUE_TRANSFER_BIT;
        s_devInfo.device_flags = MVK_DEVICE_FLAG_REALTIME_TRANSLATE |
                                 MVK_DEVICE_FLAG_SIMD_SSE2 |
                                 MVK_DEVICE_FLAG_WRITE_COMBINING |
                                 MVK_DEVICE_FLAG_ATOMIC_PRESENT;

        s_devInfo.fb_width = Graphics::GetWidth();
        s_devInfo.fb_height = Graphics::GetHeight();
        s_devInfo.fb_pitch = Graphics::GetPitch();
        CopyText(s_devInfo.layer_name, sizeof(s_devInfo.layer_name),
                 "MorphicVK 1.3 Real-Time Translation Layer");

        HAL::PCIGpuInfo gpu = {};
        if (HAL::PCI::GetPrimaryGPU(&gpu)) {
            s_devInfo.device_flags |= MVK_DEVICE_FLAG_PCI_DETECTED;
            if (gpu.vendor_id == 0x10DE || gpu.vendor_id == 0x1002 || gpu.vendor_id == 0x1022) {
                s_devInfo.device_flags |= MVK_DEVICE_FLAG_DISCRETE_GPU;
            }
            s_devInfo.vendor_id = gpu.vendor_id;
            s_devInfo.device_id = gpu.device_id;
            s_devInfo.pci_bus = gpu.bus;
            s_devInfo.pci_slot = gpu.slot;
            s_devInfo.pci_func = gpu.func;
            s_devInfo.vram_bar_phys = gpu.bar0_phys ? gpu.bar0_phys : (uint64_t)Graphics::GetVRAM();
            CopyText(s_devInfo.vendor_name, sizeof(s_devInfo.vendor_name), gpu.vendor_name);
            CopyText(s_devInfo.device_name, sizeof(s_devInfo.device_name), gpu.model_name);
        } else {
            s_devInfo.vendor_id = 0xFFFF;
            s_devInfo.device_id = 0x0001;
            s_devInfo.vram_bar_phys = (uint64_t)Graphics::GetVRAM();
            CopyText(s_devInfo.vendor_name, sizeof(s_devInfo.vendor_name), "UEFI-GOP");
            CopyText(s_devInfo.device_name, sizeof(s_devInfo.device_name), "UEFI GOP Framebuffer Adapter");
        }

        s_initialized = true;
        UART::Write("[MorphicVK] Initialized Real-Time Translation Layer over ");
        UART::Write(s_devInfo.device_name);
        UART::Write("\n");
    }

    bool QueryDevice(MorphicVkPhysicalDeviceInfo* out_info) {
        if (!out_info) return false;
        if (!s_initialized) Init();

        s_devInfo.fb_width = Graphics::GetWidth();
        s_devInfo.fb_height = Graphics::GetHeight();
        s_devInfo.fb_pitch = Graphics::GetPitch();
        s_devInfo.translated_batches = s_batches;
        s_devInfo.translated_cmds = s_commands;
        *out_info = s_devInfo;
        return true;
    }

    static void ExecFillRect(uint32_t* buf, int32_t bw, int32_t bh, uint32_t stride,
                             int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        if (w <= 0 || h <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > bw) w = bw - x;
        if (y + h > bh) h = bh - y;
        if (w <= 0 || h <= 0) return;

        for (int32_t row = 0; row < h; row++) {
            memset_fast_32(&buf[(uint64_t)(y + row) * stride + (uint64_t)x], color, (size_t)w);
        }
    }

    static void ExecBlendRect(uint32_t* buf, int32_t bw, int32_t bh, uint32_t stride,
                              int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
        if (w <= 0 || h <= 0) return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > bw) w = bw - x;
        if (y + h > bh) h = bh - y;
        if (w <= 0 || h <= 0) return;

        if (((color >> 24) & 0xFF) == 255) {
            ExecFillRect(buf, bw, bh, stride, x, y, w, h, color);
            return;
        }
        for (int32_t row = 0; row < h; row++) {
            uint32_t* dst = &buf[(uint64_t)(y + row) * stride + (uint64_t)x];
            for (int32_t col = 0; col < w; col++) {
                dst[col] = BlendARGB(dst[col], color);
            }
        }
    }

    static void ExecDrawLine(uint32_t* buf, int32_t bw, int32_t bh, uint32_t stride,
                             int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
        int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
        int32_t sx = (x0 < x1) ? 1 : -1;
        int32_t dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
        int32_t sy = (y0 < y1) ? 1 : -1;
        int32_t err = dx + dy;

        for (int32_t steps = 0; steps < 8192; steps++) {
            if (x0 >= 0 && x0 < bw && y0 >= 0 && y0 < bh) {
                uint32_t* p = &buf[(uint64_t)y0 * stride + (uint64_t)x0];
                *p = BlendARGB(*p, color);
            }
            if (x0 == x1 && y0 == y1) break;
            int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    static inline int64_t EdgeFunc(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
        return (int64_t)(px - ax) * (int64_t)(by - ay) - (int64_t)(py - ay) * (int64_t)(bx - ax);
    }

    static void ExecDrawTriangle(uint32_t* buf, int32_t bw, int32_t bh, uint32_t stride,
                                 int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                 int32_t x2, int32_t y2, uint32_t color) {
        int32_t minX = Min3(x0, x1, x2);
        int32_t minY = Min3(y0, y1, y2);
        int32_t maxX = Max3(x0, x1, x2);
        int32_t maxY = Max3(y0, y1, y2);

        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= bw) maxX = bw - 1;
        if (maxY >= bh) maxY = bh - 1;
        if (minX > maxX || minY > maxY) return;

        int64_t area = EdgeFunc(x0, y0, x1, y1, x2, y2);
        if (area == 0) return;

        // Normalize winding so positive/zero half-space test works for both CW and CCW triangles
        if (area < 0) {
            int32_t tx = x1, ty = y1;
            x1 = x2; y1 = y2;
            x2 = tx; y2 = ty;
        }

        bool opaque = (((color >> 24) & 0xFF) == 255);
        for (int32_t py = minY; py <= maxY; py++) {
            uint32_t* row = &buf[(uint64_t)py * stride];
            int32_t spanStart = -1;
            int32_t spanEnd = -1;
            for (int32_t px = minX; px <= maxX; px++) {
                if (EdgeFunc(x0, y0, x1, y1, px, py) >= 0 &&
                    EdgeFunc(x1, y1, x2, y2, px, py) >= 0 &&
                    EdgeFunc(x2, y2, x0, y0, px, py) >= 0) {
                    if (opaque) {
                        if (spanStart < 0) spanStart = px;
                        spanEnd = px;
                    } else {
                        row[px] = BlendARGB(row[px], color);
                    }
                } else if (opaque && spanStart >= 0) {
                    memset_fast_32(&row[spanStart], color, (size_t)(spanEnd - spanStart + 1));
                    spanStart = -1;
                }
            }
            if (opaque && spanStart >= 0) {
                memset_fast_32(&row[spanStart], color, (size_t)(spanEnd - spanStart + 1));
            }
        }
    }

    int ExecuteCommandPackets(uint32_t* target_buf,
                              uint32_t width,
                              uint32_t height,
                              uint32_t stride_px,
                              const MorphicVkCmdPacket* packets,
                              uint32_t cmd_count,
                              bool track_drm_damage) {
        if (!target_buf || !packets || width == 0 || height == 0) return -1;
        if (stride_px < width) stride_px = width;
        if (!s_initialized) Init();

        int32_t bw = (int32_t)width;
        int32_t bh = (int32_t)height;
        uint32_t executed = 0;

        for (uint32_t i = 0; i < cmd_count; i++) {
            const MorphicVkCmdPacket& cmd = packets[i];
            switch (cmd.opcode) {
                case MVK_CMD_NOP:
                    executed++;
                    break;

                case MVK_CMD_CLEAR_COLOR:
                    ExecFillRect(target_buf, bw, bh, stride_px, 0, 0, bw, bh, cmd.color_argb);
                    if (track_drm_damage) DRM::MarkDirty(0, 0, bw, bh);
                    executed++;
                    break;

                case MVK_CMD_FILL_RECT:
                    ExecFillRect(target_buf, bw, bh, stride_px, cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.color_argb);
                    if (track_drm_damage) DRM::MarkDirty(cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                    executed++;
                    break;

                case MVK_CMD_BLEND_RECT:
                    ExecBlendRect(target_buf, bw, bh, stride_px, cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.color_argb);
                    if (track_drm_damage) DRM::MarkDirty(cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                    executed++;
                    break;

                case MVK_CMD_DRAW_LINE:
                    ExecDrawLine(target_buf, bw, bh, stride_px, cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.color_argb);
                    if (track_drm_damage) {
                        int32_t lx = (cmd.x0 < cmd.x1) ? cmd.x0 : cmd.x1;
                        int32_t ly = (cmd.y0 < cmd.y1) ? cmd.y0 : cmd.y1;
                        int32_t lw = ((cmd.x0 > cmd.x1) ? (cmd.x0 - cmd.x1) : (cmd.x1 - cmd.x0)) + 1;
                        int32_t lh = ((cmd.y0 > cmd.y1) ? (cmd.y0 - cmd.y1) : (cmd.y1 - cmd.y0)) + 1;
                        DRM::MarkDirty(lx, ly, lw, lh);
                    }
                    executed++;
                    break;

                case MVK_CMD_DRAW_TRIANGLE:
                    ExecDrawTriangle(target_buf, bw, bh, stride_px,
                                     cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.x2, cmd.y2, cmd.color_argb);
                    if (track_drm_damage) {
                        int32_t minX = Min3(cmd.x0, cmd.x1, cmd.x2);
                        int32_t minY = Min3(cmd.y0, cmd.y1, cmd.y2);
                        int32_t maxX = Max3(cmd.x0, cmd.x1, cmd.x2);
                        int32_t maxY = Max3(cmd.y0, cmd.y1, cmd.y2);
                        DRM::MarkDirty(minX, minY, (maxX - minX) + 1, (maxY - minY) + 1);
                    }
                    executed++;
                    break;

                case MVK_CMD_COMPUTE_XOR32: {
                    // Compute shader translation primitive: SIMD-friendly 32-bit buffer transform
                    int32_t x = cmd.x0, y = cmd.y0, w = cmd.x1, h = cmd.y1;
                    if (x < 0) { w += x; x = 0; }
                    if (y < 0) { h += y; y = 0; }
                    if (x + w > bw) w = bw - x;
                    if (y + h > bh) h = bh - y;
                    for (int32_t r = 0; r < h; r++) {
                        uint32_t* row = &target_buf[(uint64_t)(y + r) * stride_px + (uint64_t)x];
                        for (int32_t c = 0; c < w; c++) {
                            row[c] ^= cmd.color_argb;
                        }
                    }
                    if (track_drm_damage && w > 0 && h > 0) DRM::MarkDirty(x, y, w, h);
                    executed++;
                    break;
                }

                case MVK_CMD_PRESENT_DAMAGE:
                    if (track_drm_damage) {
                        if (cmd.x1 > 0 && cmd.y1 > 0) {
                            DRM::MarkDirty(cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                        }
                        DRM::Present((cmd.flags & 1) != 0);
                    }
                    executed++;
                    break;

                default:
                    break;
            }
        }

        __sync_fetch_and_add(&s_batches, 1);
        __sync_fetch_and_add(&s_commands, executed);
        return (int)executed;
    }

}
