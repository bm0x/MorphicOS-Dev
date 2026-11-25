#include "xhci.h"
#include "usb_hid.h"
#include "../../hal/arch/x86_64/pci.h"
#include "../../arch/common/mmu.h"
#include "../../hal/serial/uart.h"
#include "../../mm/pmm.h"
#include "../../utils/std.h"

namespace xHCI {

    struct TRB {
        uint64_t param;
        uint32_t status;
        uint32_t control;
    };

    struct ERSTEntry {
        uint64_t ring_base;
        uint32_t ring_size;
        uint32_t rsv;
    };

    struct XHCIController {
        volatile uint8_t* cap_base;
        volatile uint8_t* op_base;
        volatile uint8_t* rt_base;
        volatile uint32_t* db_base;

        uint8_t  max_slots;
        uint8_t  max_ports;
        uint32_t connected_ports;

        uint64_t*  dcbaa;
        TRB*       cmd_ring;
        TRB*       evt_ring;
        ERSTEntry* erst;
        uint32_t   evt_deq_idx;
        uint8_t    evt_cycle;
        bool       active;
    };

    static constexpr uint32_t MAX_XHCI = 4;
    static constexpr uint32_t RING_TRBS = 64;

    static XHCIController g_ctrls[MAX_XHCI];
    static uint32_t       g_ctrl_count = 0;
    static uint32_t       g_total_ports = 0;

    static inline uint32_t Read32(volatile uint8_t* base, uint32_t off) {
        return *(volatile uint32_t*)(base + off);
    }
    static inline void Write32(volatile uint8_t* base, uint32_t off, uint32_t val) {
        *(volatile uint32_t*)(base + off) = val;
    }
    static inline void Write64(volatile uint8_t* base, uint32_t off, uint64_t val) {
        *(volatile uint32_t*)(base + off) = (uint32_t)(val & 0xFFFFFFFFULL);
        *(volatile uint32_t*)(base + off + 4) = (uint32_t)(val >> 32);
    }

    static void PerformBiosHandoff(volatile uint8_t* capBase) {
        uint32_t hccparams1 = Read32(capBase, 0x10);
        uint32_t xecp = (hccparams1 >> 16) & 0xFFFFu;
        if (xecp == 0) return;

        uint32_t off = xecp * 4u;
        for (int guard = 0; guard < 32 && off < 65536; guard++) {
            uint32_t cap = Read32(capBase, off);
            uint8_t capId = (uint8_t)(cap & 0xFFu);
            if (capId == 1) { // USB Legacy Support Capability (USBLEGSUP)
                if (cap & (1u << 16)) { // BIOS Owned
                    Write32(capBase, off, cap | (1u << 24)); // Request OS Ownership
                    for (uint32_t spin = 0; spin < 2000000; spin++) {
                        if ((Read32(capBase, off) & (1u << 16)) == 0) break;
                        __asm__ volatile("pause");
                    }
                }
                break;
            }
            uint8_t next = (uint8_t)((cap >> 8) & 0xFFu);
            if (next == 0) break;
            off += (uint32_t)next * 4u;
        }
    }

    static void InitController(const HAL::PCIDevice& dev) {
        if (g_ctrl_count >= MAX_XHCI) return;

        uint16_t pciCmd = HAL::PCI::ConfigRead16(dev.bus, dev.device, dev.function, 0x04);
        pciCmd |= 0x06u; // Memory Space + Bus Master
        HAL::PCI::ConfigWrite32(dev.bus, dev.device, dev.function, 0x04, pciCmd);

        uint64_t bar0 = HAL::PCI::GetBAR64(dev, 0) & ~0xFULL;
        if (bar0 == 0) return;

        // Map 64KB xHCI MMIO uncachable
        for (uint64_t off = 0; off < 65536; off += 4096) {
            MMU::MapPage(bar0 + off, bar0 + off, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE);
        }

        XHCIController* c = &g_ctrls[g_ctrl_count];
        kmemset(c, 0, sizeof(XHCIController));

        c->cap_base = (volatile uint8_t*)bar0;
        uint8_t capLen = *(volatile uint8_t*)(bar0 + 0x00);
        uint32_t hcsparams1 = Read32(c->cap_base, 0x04);
        uint32_t hcsparams2 = Read32(c->cap_base, 0x08);
        uint32_t dboff = Read32(c->cap_base, 0x14) & ~0x3u;
        uint32_t rtsoff = Read32(c->cap_base, 0x18) & ~0x1Fu;

        c->op_base   = c->cap_base + capLen;
        c->rt_base   = c->cap_base + rtsoff;
        c->db_base   = (volatile uint32_t*)(c->cap_base + dboff);
        c->max_slots = (uint8_t)(hcsparams1 & 0xFFu);
        c->max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFFu);

        PerformBiosHandoff(c->cap_base);

        // 1. Stop controller (USBCMD.RS = 0) and wait for HCHalted
        Write32(c->op_base, 0x00, Read32(c->op_base, 0x00) & ~1u);
        for (uint32_t i = 0; i < 1000000; i++) {
            if (Read32(c->op_base, 0x04) & 1u) break;
            __asm__ volatile("pause");
        }

        // 2. Host Controller Reset (USBCMD.HCRST = 1)
        Write32(c->op_base, 0x00, Read32(c->op_base, 0x00) | (1u << 1));
        for (uint32_t i = 0; i < 2000000; i++) {
            if (((Read32(c->op_base, 0x00) & (1u << 1)) == 0) &&
                ((Read32(c->op_base, 0x04) & (1u << 11)) == 0)) break;
            __asm__ volatile("pause");
        }

        // 3. Allocate DMA structures via PMM
        c->dcbaa    = (uint64_t*)PMM::AllocPage();
        c->cmd_ring = (TRB*)PMM::AllocPage();
        c->evt_ring = (TRB*)PMM::AllocPage();
        c->erst     = (ERSTEntry*)PMM::AllocPage();
        if (!c->dcbaa || !c->cmd_ring || !c->evt_ring || !c->erst) return;

        kmemset(c->dcbaa, 0, 4096);
        kmemset(c->cmd_ring, 0, 4096);
        kmemset(c->evt_ring, 0, 4096);
        kmemset(c->erst, 0, 4096);

        // Allocate Scratchpad buffers if requested by HCSPARAMS2
        uint32_t maxSpHi = (hcsparams2 >> 21) & 0x1Fu;
        uint32_t maxSpLo = (hcsparams2 >> 27) & 0x1Fu;
        uint32_t maxSp = (maxSpHi << 5) | maxSpLo;
        if (maxSp > 0 && maxSp <= 64) {
            uint64_t* spArray = (uint64_t*)PMM::AllocPage();
            if (spArray) {
                kmemset(spArray, 0, 4096);
                for (uint32_t s = 0; s < maxSp; s++) {
                    void* pg = PMM::AllocPage();
                    if (pg) kmemset(pg, 0, 4096);
                    spArray[s] = (uint64_t)pg;
                }
                c->dcbaa[0] = (uint64_t)spArray;
            }
        }

        // 4. Configure MaxSlotsEn, DCBAAP, CRCR
        uint32_t slots = c->max_slots > 32 ? 32 : c->max_slots;
        Write32(c->op_base, 0x38, slots);
        Write64(c->op_base, 0x30, (uint64_t)c->dcbaa);
        Write64(c->op_base, 0x18, (uint64_t)c->cmd_ring | 1ULL); // RCS=1

        // 5. Configure Interrupter 0 Event Ring (ERSTSZ, ERDP, ERSTBA)
        c->erst[0].ring_base = (uint64_t)c->evt_ring;
        c->erst[0].ring_size = RING_TRBS;
        c->evt_deq_idx = 0;
        c->evt_cycle = 1;

        volatile uint8_t* ir0 = c->rt_base + 0x20;
        Write32(ir0, 0x08, 1); // ERSTSZ = 1 segment
        Write64(ir0, 0x18, (uint64_t)c->evt_ring); // ERDP
        Write64(ir0, 0x10, (uint64_t)c->erst);     // ERSTBA

        // 6. Start xHCI controller (USBCMD.RS = 1)
        Write32(c->op_base, 0x00, Read32(c->op_base, 0x00) | 1u);
        for (uint32_t i = 0; i < 1000000; i++) {
            if ((Read32(c->op_base, 0x04) & 1u) == 0) break;
            __asm__ volatile("pause");
        }

        // 7. Scan Root Hub Ports (PORTSC at op_base + 0x400 + port * 0x10)
        for (uint32_t p = 0; p < c->max_ports; p++) {
            uint32_t portsc = Read32(c->op_base, 0x400 + p * 0x10);
            if (portsc & 1u) { // Current Connect Status (CCS)
                c->connected_ports++;
                g_total_ports++;
                uint32_t speed = (portsc >> 10) & 0x0Fu;
                UART::Write("[xHCI] Port ");
                UART::WriteDec((int64_t)(p + 1));
                UART::Write(" device connected (speed=");
                UART::WriteDec((int64_t)speed);
                UART::Write(")\n");
            }
        }

        c->active = true;
        g_ctrl_count++;

        UART::Write("[xHCI] Controller ready BAR0=");
        UART::WriteHex(bar0);
        UART::Write(" ports=");
        UART::WriteDec((int64_t)c->max_ports);
        UART::Write(" connected=");
        UART::WriteDec((int64_t)c->connected_ports);
        UART::Write("\n");
    }

    void PollEvents() {
        for (uint32_t i = 0; i < g_ctrl_count; i++) {
            XHCIController* c = &g_ctrls[i];
            if (!c->active || !c->evt_ring) continue;

            int budget = 16;
            while (budget-- > 0) {
                volatile TRB* trb = &c->evt_ring[c->evt_deq_idx];
                uint32_t ctrl = trb->control;
                if ((ctrl & 1u) != c->evt_cycle) break;

                c->evt_deq_idx++;
                if (c->evt_deq_idx >= RING_TRBS) {
                    c->evt_deq_idx = 0;
                    c->evt_cycle ^= 1u;
                }

                volatile uint8_t* ir0 = c->rt_base + 0x20;
                uint64_t nextDeq = (uint64_t)&c->evt_ring[c->evt_deq_idx];
                Write64(ir0, 0x18, nextDeq | (1ULL << 3)); // Clear EHB
            }
        }
    }

    void Init() {
        g_ctrl_count = 0;
        g_total_ports = 0;
        USB_HID::ResetState();

        uint32_t count = HAL::PCI::GetDeviceCount();
        for (uint32_t i = 0; i < count; i++) {
            HAL::PCIDevice dev = {};
            if (!HAL::PCI::GetDevice(i, &dev)) continue;

            // PCI Serial Bus (0x0C) / USB (0x03) / xHCI (0x30)
            if (dev.class_id == 0x0C && dev.subclass_id == 0x03 && dev.prog_if == 0x30) {
                InitController(dev);
            }
        }
    }

    uint32_t GetControllerCount() { return g_ctrl_count; }
    uint32_t GetConnectedPortCount() { return g_total_ports; }
}
