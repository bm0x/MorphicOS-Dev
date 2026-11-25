#pragma once
#include <stdint.h>
#include <stddef.h>
#include "morphic_syscalls.h"
#include "morphic_system_info.h"
#include "morphic_vulkan.h"
#include "graphics_uapi.h"
#include "../../shared/net_uapi.h"

// ============================================================================
// MorphicHW - Proprietary Hardware & Package Control API for Morphic OS Apps
// Safe Ring-3 SDK layer exposing GPU (MorphicVK), Display, RTC, Storage,
// Input, Power, and .mpk Package Validation/Installation.
// ============================================================================

namespace MorphicHW {

    struct MPKPackageInfo {
        bool valid;
        uint32_t version;
        uint32_t flags;
        uint64_t entry_offset;
        uint64_t code_size;
        uint64_t assets_size;
        uint64_t total_size;
    };

    namespace System {
        static inline bool GetInfo(MorphicSystemInfo* out) {
            if (!out) return false;
            return sys_get_system_info(out) != 0;
        }

        static inline bool GetDateTime(MorphicDateTime* out) {
            if (!out) return false;
            return sys_get_rtc_datetime(out) != 0;
        }

        static inline uint64_t GetUptimeMs() {
            return sys_get_time_ms();
        }

        static inline uint64_t GetPID() {
            return sys_get_pid();
        }

        static inline void SleepMs(uint32_t ms) {
            sys_sleep(ms);
        }

        static inline void Shutdown() {
            sys_shutdown();
        }

        static inline void Reboot() {
            sys_reboot();
        }
    }

    namespace Display {
        static inline void GetResolution(uint32_t* outW, uint32_t* outH) {
            uint64_t info = sys_get_screen_info();
            if (outW) *outW = (uint32_t)((info >> 32) & 0xFFFFFFFFu);
            if (outH) *outH = (uint32_t)(info & 0xFFFFFFFFu);
        }

        static inline bool QueryCaps(GraphicsUapiCaps* outCaps) {
            return MorphicGfx::QueryCaps(outCaps);
        }

        static inline uint32_t GetRefreshRate() {
            GraphicsUapiCaps caps = {};
            return MorphicGfx::QueryCaps(&caps) && caps.refresh_hz ? caps.refresh_hz : 60u;
        }

        static inline uint32_t GetMeasuredFps() {
            GraphicsUapiCaps caps = {};
            return MorphicGfx::QueryCaps(&caps) ? caps.measured_fps : 0u;
        }

        static inline uint32_t GetLastFrameUs() {
            GraphicsUapiCaps caps = {};
            return MorphicGfx::QueryCaps(&caps) ? caps.last_frame_us : 0u;
        }

        static inline bool SetRefreshRate(uint32_t hz, uint32_t vsyncMode = GRAPHICS_VSYNC_ADAPTIVE) {
            return MorphicGfx::SetDisplayMode(hz, vsyncMode) != 0;
        }
    }

    namespace GPU {
        static inline bool QueryDevice(MorphicVkPhysicalDeviceInfo* outInfo) {
            return MorphicVK::QueryPhysicalDevice(outInfo);
        }

        static inline bool IsAccelerated() {
            MorphicVkPhysicalDeviceInfo info = {};
            return QueryDevice(&info) && info.vendor_id != 0;
        }
    }

    namespace Network {
        static inline bool GetInfo(MorphicNetInfo* outInfo) {
            if (!outInfo) return false;
            return sys_net_info(outInfo) != 0;
        }

        static inline bool IsLinkUp() {
            MorphicNetInfo ni = {};
            return GetInfo(&ni) && ni.link_up != 0;
        }
    }

    namespace Input {
        static inline void GetMouseState(int* outX, int* outY, uint8_t* outButtons) {
            uint64_t raw = sys_get_mouse_state();
            if (outX) *outX = (int)((raw >> 32) & 0xFFFFu);
            if (outY) *outY = (int)((raw >> 16) & 0xFFFFu);
            if (outButtons) *outButtons = (uint8_t)(raw & 0xFFu);
        }

        static inline uint64_t GetDroppedEventCount() {
            return sys_get_input_drop_count();
        }

        static inline bool SetKeymap(const char* layoutId) {
            return sys_set_keymap(layoutId) == 0;
        }
    }

    namespace Storage {
        static inline int ListMounts(MountEntry* entries, int maxEntries) {
            return sys_list_mounts(entries, maxEntries);
        }

        static inline int ListDir(const char* path, DirEntry* entries, int maxEntries) {
            return sys_readdir(path, entries, maxEntries);
        }

        static inline int ReadFile(const char* path, void* buf, int maxBytes) {
            return sys_read_file(path, buf, maxBytes);
        }

        static inline int WriteFile(const char* path, const void* buf, uint32_t bytes) {
            return sys_write_file(path, buf, bytes);
        }

        static inline int CreateDir(const char* path) {
            return sys_mkdir(path);
        }

        static inline int DeleteFile(const char* path) {
            return sys_delete_file(path);
        }

        // Copy file via VFS using dynamically allocated Ring-3 buffer
        static inline int CopyFile(const char* srcPath, const char* dstPath, uint32_t maxBytes = 4 * 1024 * 1024) {
            void* buf = sys_malloc(maxBytes);
            if (!buf) return -1;
            int readBytes = sys_read_file(srcPath, buf, (int)maxBytes);
            if (readBytes <= 0) {
                sys_free(buf);
                return -2;
            }
            int written = sys_write_file(dstPath, buf, (uint32_t)readBytes);
            sys_free(buf);
            return (written > 0) ? readBytes : -3;
        }
    }

    namespace Package {
        // Parse and validate MPK1 binary header (64 bytes, matches tools/mpk_pack.py)
        static inline bool ValidateHeader(const void* data, uint64_t size, MPKPackageInfo* out) {
            if (out) {
                out->valid = false;
                out->version = 0;
                out->flags = 0;
                out->entry_offset = 0;
                out->code_size = 0;
                out->assets_size = 0;
                out->total_size = size;
            }
            if (!data || size < 64) return false;
            const uint8_t* b = (const uint8_t*)data;
            if (b[0] != 'M' || b[1] != 'P' || b[2] != 'K' || b[3] != '1') return false;

            auto read_u32 = [](const uint8_t* p) -> uint32_t {
                return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            };
            auto read_u64 = [](const uint8_t* p) -> uint64_t {
                uint64_t v = 0;
                for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (i * 8));
                return v;
            };

            uint32_t ver = read_u32(b + 4);
            uint32_t flg = read_u32(b + 8);
            uint64_t entry = read_u64(b + 16);
            uint64_t codeOff = read_u64(b + 24);
            uint64_t codeSz = read_u64(b + 32);
            uint64_t assetOff = read_u64(b + 40);
            uint64_t assetSz = read_u64(b + 48);

            if (codeOff + codeSz > size) return false;
            if (assetSz > 0 && assetOff + assetSz > size) return false;

            if (out) {
                out->valid = true;
                out->version = ver;
                out->flags = flg;
                out->entry_offset = entry;
                out->code_size = codeSz;
                out->assets_size = assetSz;
                out->total_size = size;
            }
            return true;
        }

        // Install a validated .mpk package into target directory (e.g. "/data/apps")
        // Returns installed byte count (>0) on success, negative error code on failure.
        static inline int Install(const char* srcPath, const char* appFileName, const char* targetDir = "/data/apps") {
            if (!srcPath || !appFileName || !targetDir) return -1;

            const uint32_t MAX_MPK = 4 * 1024 * 1024;
            void* buf = sys_malloc(MAX_MPK);
            if (!buf) return -2;

            int len = sys_read_file(srcPath, buf, (int)MAX_MPK);
            if (len < 64) {
                sys_free(buf);
                return -3;
            }

            MPKPackageInfo info = {};
            if (!ValidateHeader(buf, (uint64_t)len, &info)) {
                sys_free(buf);
                return -4; // Invalid MPK1 signature or corrupt header
            }

            // Ensure target directory exists (ignore error if already exists)
            sys_mkdir(targetDir);

            char dstPath[128];
            int p = 0;
            for (int i = 0; targetDir[i] && p < 120; i++) dstPath[p++] = targetDir[i];
            if (p > 0 && dstPath[p - 1] != '/') dstPath[p++] = '/';
            for (int i = 0; appFileName[i] && p < 126; i++) dstPath[p++] = appFileName[i];
            dstPath[p] = '\0';

            int written = sys_write_file(dstPath, buf, (uint32_t)len);
            sys_free(buf);
            return (written > 0) ? len : -5;
        }

        static inline int Launch(const char* mpkPath) {
            if (!mpkPath) return -1;
            return sys_spawn(mpkPath);
        }
    }

}
