#include "network_manager.h"
#include "../serial/uart.h"
#include "../../utils/std.h"

namespace NetworkManager {

    static constexpr uint32_t MAX_NET_DEVICES = 8;
    static INetworkDevice* g_devices[MAX_NET_DEVICES] = {nullptr};
    static uint32_t g_device_count = 0;

    void Init() {
        g_device_count = 0;
        for (uint32_t i = 0; i < MAX_NET_DEVICES; i++) g_devices[i] = nullptr;
    }

    void RegisterDevice(INetworkDevice* dev) {
        if (!dev || g_device_count >= MAX_NET_DEVICES) return;
        g_devices[g_device_count++] = dev;
        UART::Write("[NetworkHAL] Registered NIC: ");
        UART::Write(dev->name);
        UART::Write("\n");
    }

    uint32_t GetDeviceCount() { return g_device_count; }

    INetworkDevice* GetPrimary() {
        return (g_device_count > 0) ? g_devices[0] : nullptr;
    }

    bool GetPrimaryInfo(MorphicNetInfo* out) {
        if (!out) return false;
        kmemset(out, 0, sizeof(MorphicNetInfo));
        INetworkDevice* dev = GetPrimary();
        if (!dev || !dev->get_info) return false;
        return dev->get_info(dev, out);
    }

    bool SendPacket(const void* data, uint16_t len) {
        INetworkDevice* dev = GetPrimary();
        if (!dev || !dev->send_packet) return false;
        return dev->send_packet(dev, data, len);
    }

}
