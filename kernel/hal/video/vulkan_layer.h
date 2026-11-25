#pragma once
#include <stdint.h>
#include "../../../shared/vulkan_uapi.h"

namespace VulkanLayer {
    // Initialize the real-time Vulkan 1.3 hardware translation layer
    // Binds to detected PCI GPU (NVIDIA/AMD/Intel/VirtIO/Bochs) + GOP/DRM + SIMD
    void Init();

    // Populate Vulkan Physical Device properties & translation telemetry
    bool QueryDevice(MorphicVkPhysicalDeviceInfo* out_info);

    // Execute a stream of MorphicVkCmdPacket commands on a target pixel buffer in real time
    // Returns number of commands translated, or -1 on error
    int ExecuteCommandPackets(uint32_t* target_buf,
                              uint32_t width,
                              uint32_t height,
                              uint32_t stride_px,
                              const MorphicVkCmdPacket* packets,
                              uint32_t cmd_count,
                              bool track_drm_damage);
}
