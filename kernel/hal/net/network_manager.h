#pragma once
#include <stdint.h>
#include "../../../shared/net_uapi.h"

struct INetworkDevice;
typedef bool (*NetSendFunc)(INetworkDevice* dev, const void* data, uint16_t len);
typedef bool (*NetInfoFunc)(INetworkDevice* dev, MorphicNetInfo* out);

struct INetworkDevice {
    char name[16];
    void* driver_data;
    NetSendFunc send_packet;
    NetInfoFunc get_info;
};

namespace NetworkManager {
    void Init();
    void RegisterDevice(INetworkDevice* dev);
    uint32_t GetDeviceCount();
    INetworkDevice* GetPrimary();
    bool GetPrimaryInfo(MorphicNetInfo* out);
    bool SendPacket(const void* data, uint16_t len);
}
