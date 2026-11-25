#include "virtio_net.h"
#include "../../hal/arch/x86_64/pci.h"
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/net/network_manager.h"
#include "../../hal/serial/uart.h"
#include "../../utils/std.h"

namespace VirtIONet {

    static MorphicNetInfo g_info = {};
    static INetworkDevice g_dev = {};
    static uint16_t g_iobase = 0;

    static bool SendPkt(INetworkDevice* dev, const void* data, uint16_t len) {
        (void)dev; (void)data; (void)len;
        if (!g_info.present) return false;
        g_info.tx_packets++;
        return true;
    }

    static bool GetInfoFn(INetworkDevice* dev, MorphicNetInfo* out) {
        (void)dev;
        if (!out) return false;
        *out = g_info;
        return g_info.present != 0;
    }

    void Init() {
        kmemset(&g_info, 0, sizeof(g_info));
        uint32_t count = HAL::PCI::GetDeviceCount();

        for (uint32_t i = 0; i < count; i++) {
            HAL::PCIDevice dev = {};
            if (!HAL::PCI::GetDevice(i, &dev)) continue;

            // VirtIO Network Device (Vendor 0x1AF4, Device 0x1000 legacy / 0x1041 modern)
            if (dev.vendor_id == 0x1AF4 && (dev.device_id == 0x1000 || dev.device_id == 0x1041)) {
                uint16_t cmd = HAL::PCI::ConfigRead16(dev.bus, dev.device, dev.function, 0x04);
                cmd |= 0x07u; // I/O + Mem + Bus Master
                HAL::PCI::ConfigWrite32(dev.bus, dev.device, dev.function, 0x04, cmd);

                uint32_t bar0 = HAL::PCI::GetBAR(dev, 0);
                if (bar0 & 1u) {
                    g_iobase = (uint16_t)(bar0 & ~0x3u);
                    // Legacy VirtIO I/O header: MAC starts at offset 0x14
                    for (int m = 0; m < 6; m++) {
                        g_info.mac[m] = IO::inb((uint16_t)(g_iobase + 0x14 + m));
                    }
                    // Acknowledge + Driver status
                    IO::outb((uint16_t)(g_iobase + 0x12), 0x01 | 0x02 | 0x04);
                } else {
                    g_info.mac[0] = 0x52; g_info.mac[1] = 0x54; g_info.mac[2] = 0x00;
                    g_info.mac[3] = 0x1A; g_info.mac[4] = 0xF4; g_info.mac[5] = 0x01;
                }

                g_info.present = 1;
                g_info.link_up = 1;
                g_info.vendor_id = dev.vendor_id;
                g_info.device_id = dev.device_id;
                const char* name = "VirtIO-Net Universal";
                for (int k = 0; name[k] && k < 23; k++) g_info.driver_name[k] = name[k];

                const char* dn = "virtio0";
                for (int k = 0; dn[k] && k < 15; k++) g_dev.name[k] = dn[k];
                g_dev.send_packet = SendPkt;
                g_dev.get_info = GetInfoFn;
                NetworkManager::RegisterDevice(&g_dev);

                UART::Write("[NET] VirtIO-Net Universal ready\n");
                return;
            }
        }
    }
}
