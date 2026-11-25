#pragma once
#include <stdint.h>
#include "../../shared/vulkan_uapi.h"
#include "morphic_syscalls.h"

// ============================================================================
// MorphicVK 1.3 Userspace SDK Header
// Real-Time Vulkan Translation Layer for Morphic OS (.mpk Applications)
// ============================================================================

namespace MorphicVK {

    static inline bool QueryPhysicalDevice(MorphicVkPhysicalDeviceInfo* out_info) {
        if (!out_info) return false;
        return sys_vk_query_device(out_info) == 1;
    }

    template <uint32_t Capacity = 256>
    class CommandBuffer {
    public:
        CommandBuffer() : count_(0) {}

        void Begin() { count_ = 0; }

        bool CmdClearColor(uint32_t argb) {
            return Push({MVK_CMD_CLEAR_COLOR, 0, argb, 0, 0, 0, 0, 0, 0, 0});
        }

        bool CmdFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t argb) {
            return Push({MVK_CMD_FILL_RECT, 0, argb, x, y, w, h, 0, 0, 0});
        }

        bool CmdBlendRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t argb) {
            return Push({MVK_CMD_BLEND_RECT, 0, argb, x, y, w, h, 0, 0, 0});
        }

        bool CmdDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t argb) {
            return Push({MVK_CMD_DRAW_LINE, 0, argb, x0, y0, x1, y1, 0, 0, 0});
        }

        bool CmdDrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2, uint32_t argb) {
            return Push({MVK_CMD_DRAW_TRIANGLE, 0, argb, x0, y0, x1, y1, x2, y2, 0});
        }

        bool CmdComputeXor32(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t mask) {
            return Push({MVK_CMD_COMPUTE_XOR32, 0, mask, x, y, w, h, 0, 0, 0});
        }

        bool CmdPresent(int16_t x = 0, int16_t y = 0, int16_t w = 0, int16_t h = 0, bool vsync = true) {
            return Push({MVK_CMD_PRESENT_DAMAGE, (uint16_t)(vsync ? 1 : 0), 0, x, y, w, h, 0, 0, 0});
        }

        int SubmitToSurface(void* surface_ptr, uint32_t width, uint32_t height, uint32_t stride_px = 0) const {
            if (count_ == 0) return 0;
            MorphicVkSubmitInfo submit = {};
            submit.target_surface_virt = (uint64_t)surface_ptr;
            submit.target_width = width;
            submit.target_height = height;
            submit.target_stride_px = stride_px ? stride_px : width;
            submit.cmd_count = count_;
            submit.cmd_packets_ptr = (uint64_t)&packets_[0];
            submit.flags = (surface_ptr == nullptr) ? 1u : 0u;
            return sys_vk_submit(&submit);
        }

        uint32_t GetCount() const { return count_; }

    private:
        MorphicVkCmdPacket packets_[Capacity];
        uint32_t count_;

        bool Push(const MorphicVkCmdPacket& pkt) {
            if (count_ >= Capacity) return false;
            packets_[count_++] = pkt;
            return true;
        }
    };

}
