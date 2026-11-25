#include "e1000.h"
#include "../../hal/arch/x86_64/pci.h"
#include "../../arch/common/mmu.h"
#include "../../hal/net/network_manager.h"
#include "../../hal/serial/uart.h"
#include "../../mm/pmm.h"
#include "../../utils/std.h"

namespace NetE1000 {

    struct TxDesc {
        uint64_t addr;
        uint16_t length;
        uint8_t  cso;
        uint8_t  cmd;
        uint8_t  status;
        uint8_t  css;
        uint16_t special;
    };

    struct RxDesc {
        uint64_t addr;
        uint16_t length;
        uint16_t checksum;
        uint8_t  status;
        uint8_t  errors;
        uint16_t special;
    };

    static constexpr uint32_t REG_CTRL   = 0x0000;
    static constexpr uint32_t REG_STATUS = 0x0008;
    static constexpr uint32_t REG_EERD   = 0x0014;
    static constexpr uint32_t REG_RCTL   = 0x0100;
    static constexpr uint32_t REG_TCTL   = 0x0400;
    static constexpr uint32_t REG_RDBAL  = 0x2800;
    static constexpr uint32_t REG_RDBAH  = 0x2804;
    static constexpr uint32_t REG_RDLEN  = 0x2808;
    static constexpr uint32_t REG_RDH    = 0x2810;
    static constexpr uint32_t REG_RDT    = 0x2818;
    static constexpr uint32_t REG_TDBAL  = 0x3800;
    static constexpr uint32_t REG_TDBAH  = 0x3804;
    static constexpr uint32_t REG_TDLEN  = 0x3808;
    static constexpr uint32_t REG_TDH    = 0x3810;
    static constexpr uint32_t REG_TDT    = 0x3818;
    static constexpr uint32_t REG_RAL0   = 0x5400;
    static constexpr uint32_t REG_RAH0   = 0x5404;

    static constexpr uint32_t NUM_DESC = 16;

    static volatile uint8_t* g_mmio = nullptr;
    static TxDesc*  g_tx_ring = nullptr;
    static RxDesc*  g_rx_ring = nullptr;
    static void*    g_tx_bufs[NUM_DESC] = {nullptr};
    static void*    g_rx_bufs[NUM_DESC] = {nullptr};
    static uint32_t g_tx_tail = 0;

    static MorphicNetInfo g_info = {};

    static inline uint32_t Read32(uint32_t off) {
        return *(volatile uint32_t*)(g_mmio + off);
    }
    static inline void Write32(uint32_t off, uint32_t val) {
        *(volatile uint32_t*)(g_mmio + off) = val;
    }

    static bool ReadEeprom(uint8_t addr, uint16_t* outVal) {
        Write32(REG_EERD, 1u | ((uint32_t)addr << 8));
        for (uint32_t spin = 0; spin < 200000; spin++) {
            uint32_t v = Read32(REG_EERD);
            if (v & (1u << 4)) {
                *outVal = (uint16_t)(v >> 16);
                return true;
            }
            __asm__ volatile("pause");
        }
        return false;
    }

    static void ReadMacAddress() {
        uint16_t w0 = 0, w1 = 0, w2 = 0;
        if (ReadEeprom(0, &w0) && ReadEeprom(1, &w1) && ReadEeprom(2, &w2) &&
            (w0 != 0xFFFF || w1 != 0xFFFF)) {
            g_info.mac[0] = (uint8_t)(w0 & 0xFF);
            g_info.mac[1] = (uint8_t)(w0 >> 8);
            g_info.mac[2] = (uint8_t)(w1 & 0xFF);
            g_info.mac[3] = (uint8_t)(w1 >> 8);
            g_info.mac[4] = (uint8_t)(w2 & 0xFF);
            g_info.mac[5] = (uint8_t)(w2 >> 8);
            return;
        }
        // Fallback to RAL0 / RAH0 MMIO registers
        uint32_t ral = Read32(REG_RAL0);
        uint32_t rah = Read32(REG_RAH0);
        g_info.mac[0] = (uint8_t)(ral & 0xFF);
        g_info.mac[1] = (uint8_t)((ral >> 8) & 0xFF);
        g_info.mac[2] = (uint8_t)((ral >> 16) & 0xFF);
        g_info.mac[3] = (uint8_t)((ral >> 24) & 0xFF);
        g_info.mac[4] = (uint8_t)(rah & 0xFF);
        g_info.mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    }

    static bool IsSupportedIntelNIC(uint16_t vendor, uint16_t device) {
        if (vendor != 0x8086) return false;
        return (device == 0x100E || device == 0x100F || device == 0x10D3 ||
                device == 0x153A || device == 0x1004 || device == 0x10EA);
    }

    void Init() {
        kmemset(&g_info, 0, sizeof(g_info));

        uint32_t count = HAL::PCI::GetDeviceCount();
        for (uint32_t i = 0; i < count; i++) {
            HAL::PCIDevice dev = {};
            if (!HAL::PCI::GetDevice(i, &dev)) continue;

            if (dev.class_id == 0x02 && dev.subclass_id == 0x00 &&
                IsSupportedIntelNIC(dev.vendor_id, dev.device_id)) {

                uint16_t cmd = HAL::PCI::ConfigRead16(dev.bus, dev.device, dev.function, 0x04);
                cmd |= 0x06u; // Memory Space + Bus Master
                HAL::PCI::ConfigWrite32(dev.bus, dev.device, dev.function, 0x04, cmd);

                uint64_t bar0 = HAL::PCI::GetBAR64(dev, 0) & ~0xFULL;
                if (bar0 == 0) continue;

                // Map 128KB BAR0 MMIO uncachable
                for (uint64_t off = 0; off < 131072; off += 4096) {
                    MMU::MapPage(bar0 + off, bar0 + off, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE);
                }
                g_mmio = (volatile uint8_t*)bar0;

                // Set Link Up (SLU = bit 6)
                Write32(REG_CTRL, Read32(REG_CTRL) | (1u << 6));
                ReadMacAddress();

                // Allocate TX & RX DMA rings via PMM
                g_rx_ring = (RxDesc*)PMM::AllocPage();
                g_tx_ring = (TxDesc*)PMM::AllocPage();
                if (!g_rx_ring || !g_tx_ring) return;
                kmemset(g_rx_ring, 0, 4096);
                kmemset(g_tx_ring, 0, 4096);

                for (uint32_t d = 0; d < NUM_DESC; d++) {
                    g_rx_bufs[d] = PMM::AllocPage();
                    g_tx_bufs[d] = PMM::AllocPage();
                    g_rx_ring[d].addr = (uint64_t)g_rx_bufs[d];
                    g_tx_ring[d].addr = (uint64_t)g_tx_bufs[d];
                    g_tx_ring[d].status = 1; // DD
                }

                // Program RX ring
                Write32(REG_RDBAL, (uint32_t)((uint64_t)g_rx_ring & 0xFFFFFFFFULL));
                Write32(REG_RDBAH, (uint32_t)((uint64_t)g_rx_ring >> 32));
                Write32(REG_RDLEN, NUM_DESC * sizeof(RxDesc));
                Write32(REG_RDH, 0);
                Write32(REG_RDT, NUM_DESC - 1);
                Write32(REG_RCTL, (1u << 1) | (1u << 15) | (1u << 26)); // EN | BAM | SECRC

                // Program TX ring
                Write32(REG_TDBAL, (uint32_t)((uint64_t)g_tx_ring & 0xFFFFFFFFULL));
                Write32(REG_TDBAH, (uint32_t)((uint64_t)g_tx_ring >> 32));
                Write32(REG_TDLEN, NUM_DESC * sizeof(TxDesc));
                Write32(REG_TDH, 0);
                Write32(REG_TDT, 0);
                Write32(REG_TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12)); // EN | PSP

                g_info.present = 1;
                g_info.link_up = (Read32(REG_STATUS) & (1u << 1)) ? 1u : 0u;
                g_info.vendor_id = dev.vendor_id;
                g_info.device_id = dev.device_id;
                const char* drv = "Intel e1000 Gigabit";
                for (int k = 0; drv[k] && k < 23; k++) g_info.driver_name[k] = drv[k];

                static INetworkDevice s_e1000_dev = {};
                const char* dn = "eth0";
                for (int k = 0; dn[k] && k < 15; k++) s_e1000_dev.name[k] = dn[k];
                s_e1000_dev.send_packet = [](INetworkDevice*, const void* d, uint16_t l) { return SendPacket(d, l); };
                s_e1000_dev.get_info    = [](INetworkDevice*, MorphicNetInfo* o) { return GetInfo(o); };
                NetworkManager::RegisterDevice(&s_e1000_dev);

                UART::Write("[NET] Intel e1000 ready BAR0=");
                UART::WriteHex(bar0);
                UART::Write(" Link=");
                UART::Write(g_info.link_up ? "UP" : "DOWN");
                UART::Write(" MAC=");
                for (int m = 0; m < 6; m++) {
                    const char* hx = "0123456789ABCDEF";
                    UART::PutChar(hx[(g_info.mac[m] >> 4) & 0xF]);
                    UART::PutChar(hx[g_info.mac[m] & 0xF]);
                    if (m < 5) UART::PutChar(':');
                }
                UART::Write("\n");
                return;
            }
        }
    }

    bool IsPresent() { return g_info.present != 0; }
    bool IsLinkUp() {
        if (!g_mmio || !g_info.present) return false;
        g_info.link_up = (Read32(REG_STATUS) & (1u << 1)) ? 1u : 0u;
        return g_info.link_up != 0;
    }

    bool GetInfo(MorphicNetInfo* out) {
        if (!out) return false;
        if (g_mmio && g_info.present) {
            g_info.link_up = (Read32(REG_STATUS) & (1u << 1)) ? 1u : 0u;
        }
        *out = g_info;
        return g_info.present != 0;
    }

    bool SendPacket(const void* data, uint16_t length) {
        if (!g_mmio || !g_info.present || !data || length == 0 || length > 1518) return false;
        uint32_t idx = g_tx_tail;
        kmemcpy(g_tx_bufs[idx], data, length);
        g_tx_ring[idx].length = length;
        g_tx_ring[idx].cmd = (1u << 0) | (1u << 1) | (1u << 3); // EOP | IFCS | RS
        g_tx_ring[idx].status = 0;

        g_tx_tail = (idx + 1) % NUM_DESC;
        Write32(REG_TDT, g_tx_tail);
        g_info.tx_packets++;
        return true;
    }
}
