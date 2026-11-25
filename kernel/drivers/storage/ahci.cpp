#include "ahci.h"
#include "../../hal/arch/x86_64/pci.h"
#include "../../arch/common/mmu.h"
#include "../../hal/storage/block_device.h"
#include "../../hal/serial/uart.h"
#include "../../mm/pmm.h"
#include "../../utils/std.h"

namespace AHCI {

    struct HBAPort {
        uint32_t clb;
        uint32_t clbu;
        uint32_t fb;
        uint32_t fbu;
        uint32_t is;
        uint32_t ie;
        uint32_t cmd;
        uint32_t rsv0;
        uint32_t tfd;
        uint32_t sig;
        uint32_t ssts;
        uint32_t sctl;
        uint32_t serr;
        uint32_t sact;
        uint32_t ci;
        uint32_t sntf;
        uint32_t fbs;
        uint32_t rsv1[11];
        uint32_t vendor[4];
    };

    struct HBAMem {
        uint32_t cap;
        uint32_t ghc;
        uint32_t is;
        uint32_t pi;
        uint32_t vs;
        uint32_t ccc_ctl;
        uint32_t ccc_pts;
        uint32_t em_loc;
        uint32_t em_ctl;
        uint32_t cap2;
        uint32_t bohc;
        uint8_t  rsv[0xA0 - 0x2C];
        uint8_t  vendor[0x100 - 0xA0];
        HBAPort  ports[32];
    };

    struct HBACmdHeader {
        uint8_t  cfl : 5;
        uint8_t  a   : 1;
        uint8_t  w   : 1;
        uint8_t  p   : 1;
        uint8_t  r   : 1;
        uint8_t  b   : 1;
        uint8_t  c   : 1;
        uint8_t  rsv0: 1;
        uint8_t  pmp : 4;
        uint16_t prdtl;
        volatile uint32_t prdbc;
        uint32_t ctba;
        uint32_t ctbau;
        uint32_t rsv1[4];
    };

    struct HBAPrdtEntry {
        uint32_t dba;
        uint32_t dbau;
        uint32_t rsv0;
        uint32_t dbc : 22;
        uint32_t rsv1: 9;
        uint32_t i   : 1;
    };

    struct HBACmdTable {
        uint8_t      cfis[64];
        uint8_t      acmd[16];
        uint8_t      rsv[48];
        HBAPrdtEntry prdt[1];
    };

    struct FisRegH2D {
        uint8_t  fis_type;
        uint8_t  pmport : 4;
        uint8_t  rsv0   : 3;
        uint8_t  c      : 1;
        uint8_t  command;
        uint8_t  featurel;
        uint8_t  lba0;
        uint8_t  lba1;
        uint8_t  lba2;
        uint8_t  device;
        uint8_t  lba3;
        uint8_t  lba4;
        uint8_t  lba5;
        uint8_t  featureh;
        uint8_t  countl;
        uint8_t  counth;
        uint8_t  icc;
        uint8_t  control;
        uint8_t  rsv1[4];
    };

    struct AHCIDrive {
        volatile HBAPort* port;
        HBACmdHeader*     cmd_list;
        HBACmdTable*      cmd_table;
        void*             fis_buf;
        void*             dma_buf; // 4KB DMA bounce page
        IBlockDevice      block_dev;
        bool              active;
    };

    static constexpr uint32_t MAX_AHCI_DRIVES = 8;
    static constexpr uint32_t SATA_SIG_ATA    = 0x00000101;
    static constexpr uint8_t  FIS_TYPE_REG_H2D = 0x27;
    static constexpr uint8_t  ATA_CMD_READ_DMA_EX  = 0x25;
    static constexpr uint8_t  ATA_CMD_WRITE_DMA_EX = 0x35;
    static constexpr uint8_t  ATA_CMD_IDENTIFY     = 0xEC;

    static AHCIDrive g_drives[MAX_AHCI_DRIVES];
    static uint32_t  g_drive_count = 0;

    static void StopPort(volatile HBAPort* port) {
        port->cmd &= ~0x0001u; // ST = 0
        port->cmd &= ~0x0010u; // FRE = 0
        for (uint32_t i = 0; i < 500000; i++) {
            if ((port->cmd & 0xC000u) == 0) break; // FR & CR cleared
            __asm__ volatile("pause");
        }
    }

    static void StartPort(volatile HBAPort* port) {
        for (uint32_t i = 0; i < 500000; i++) {
            if ((port->cmd & 0x8000u) == 0) break;
            __asm__ volatile("pause");
        }
        port->cmd |= 0x0010u; // FRE = 1
        port->cmd |= 0x0001u; // ST = 1
    }

    static bool IssueATA(AHCIDrive* drv, uint8_t cmd, uint64_t lba, uint16_t count, bool isWrite) {
        volatile HBAPort* port = drv->port;
        port->is = 0xFFFFFFFFu;

        // Wait until port is not busy (BSY | DRQ)
        for (uint32_t spin = 0; spin < 1000000; spin++) {
            if ((port->tfd & 0x88u) == 0) break;
            if (spin == 999999) return false;
            __asm__ volatile("pause");
        }

        HBACmdHeader* hdr = &drv->cmd_list[0];
        hdr->cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        hdr->w = isWrite ? 1 : 0;
        hdr->prdtl = 1;
        hdr->prdbc = 0;

        HBACmdTable* tbl = drv->cmd_table;
        kmemset(tbl, 0, sizeof(HBACmdTable));

        uint64_t dmaPhys = (uint64_t)drv->dma_buf;
        tbl->prdt[0].dba  = (uint32_t)(dmaPhys & 0xFFFFFFFFULL);
        tbl->prdt[0].dbau = (uint32_t)(dmaPhys >> 32);
        tbl->prdt[0].dbc  = (uint32_t)(count * 512u - 1u);
        tbl->prdt[0].i    = 1;

        FisRegH2D* fis = (FisRegH2D*)tbl->cfis;
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c        = 1;
        fis->command  = cmd;
        fis->device   = (cmd == ATA_CMD_IDENTIFY) ? 0 : (1u << 6); // LBA mode
        fis->lba0     = (uint8_t)(lba & 0xFF);
        fis->lba1     = (uint8_t)((lba >> 8) & 0xFF);
        fis->lba2     = (uint8_t)((lba >> 16) & 0xFF);
        fis->lba3     = (uint8_t)((lba >> 24) & 0xFF);
        fis->lba4     = (uint8_t)((lba >> 32) & 0xFF);
        fis->lba5     = (uint8_t)((lba >> 40) & 0xFF);
        fis->countl   = (uint8_t)(count & 0xFF);
        fis->counth   = (uint8_t)((count >> 8) & 0xFF);

        port->ci = 1u;

        for (uint32_t spin = 0; spin < 2000000; spin++) {
            if ((port->ci & 1u) == 0) break;
            if (port->is & (1u << 30)) return false; // Task file error
            if (spin == 1999999) return false;
            __asm__ volatile("pause");
        }
        return (port->is & (1u << 30)) == 0;
    }

    static bool ReadBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!dev || !dev->driver_data || !buffer || count == 0) return false;
        AHCIDrive* drv = (AHCIDrive*)dev->driver_data;
        uint8_t* dst = (uint8_t*)buffer;

        while (count > 0) {
            uint16_t chunk = (count > 8) ? 8 : (uint16_t)count; // 8 sectors = 4KB DMA page
            if (!IssueATA(drv, ATA_CMD_READ_DMA_EX, lba, chunk, false)) return false;
            kmemcpy(dst, drv->dma_buf, (size_t)chunk * 512u);
            dst += (size_t)chunk * 512u;
            lba += chunk;
            count -= chunk;
        }
        return true;
    }

    static bool WriteBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!dev || !dev->driver_data || !buffer || count == 0) return false;
        AHCIDrive* drv = (AHCIDrive*)dev->driver_data;
        const uint8_t* src = (const uint8_t*)buffer;

        while (count > 0) {
            uint16_t chunk = (count > 8) ? 8 : (uint16_t)count;
            kmemcpy(drv->dma_buf, src, (size_t)chunk * 512u);
            if (!IssueATA(drv, ATA_CMD_WRITE_DMA_EX, lba, chunk, true)) return false;
            src += (size_t)chunk * 512u;
            lba += chunk;
            count -= chunk;
        }
        return true;
    }

    static void SetupPort(volatile HBAPort* port) {
        if (g_drive_count >= MAX_AHCI_DRIVES) return;

        uint32_t ssts = port->ssts;
        uint8_t det = (uint8_t)(ssts & 0x0F);
        uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
        if (det != 3 || ipm != 1) return;
        if (port->sig != SATA_SIG_ATA) return;

        AHCIDrive* drv = &g_drives[g_drive_count];
        kmemset(drv, 0, sizeof(AHCIDrive));
        drv->port = port;

        StopPort(port);

        drv->cmd_list  = (HBACmdHeader*)PMM::AllocPage();
        drv->fis_buf   = PMM::AllocPage();
        drv->cmd_table = (HBACmdTable*)PMM::AllocPage();
        drv->dma_buf   = PMM::AllocPage();
        if (!drv->cmd_list || !drv->fis_buf || !drv->cmd_table || !drv->dma_buf) return;

        kmemset(drv->cmd_list, 0, 4096);
        kmemset(drv->fis_buf, 0, 4096);
        kmemset(drv->cmd_table, 0, 4096);
        kmemset(drv->dma_buf, 0, 4096);

        uint64_t clbPhys = (uint64_t)drv->cmd_list;
        uint64_t fbPhys  = (uint64_t)drv->fis_buf;
        uint64_t ctPhys  = (uint64_t)drv->cmd_table;

        port->clb  = (uint32_t)(clbPhys & 0xFFFFFFFFULL);
        port->clbu = (uint32_t)(clbPhys >> 32);
        port->fb   = (uint32_t)(fbPhys & 0xFFFFFFFFULL);
        port->fbu  = (uint32_t)(fbPhys >> 32);

        drv->cmd_list[0].ctba  = (uint32_t)(ctPhys & 0xFFFFFFFFULL);
        drv->cmd_list[0].ctbau = (uint32_t)(ctPhys >> 32);

        port->serr = 0xFFFFFFFFu;
        port->is   = 0xFFFFFFFFu;
        StartPort(port);

        if (!IssueATA(drv, ATA_CMD_IDENTIFY, 0, 1, false)) {
            return;
        }

        uint16_t* id = (uint16_t*)drv->dma_buf;
        uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                         ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
        uint64_t lba28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
        uint64_t sectors = (lba48 > 0) ? lba48 : lba28;
        if (sectors == 0) return;

        drv->block_dev.name[0] = 's';
        drv->block_dev.name[1] = 'a';
        drv->block_dev.name[2] = 't';
        drv->block_dev.name[3] = 'a';
        drv->block_dev.name[4] = (char)('0' + g_drive_count);
        drv->block_dev.name[5] = '\0';

        drv->block_dev.geometry.sector_size = 512;
        drv->block_dev.geometry.total_sectors = sectors;
        drv->block_dev.geometry.total_bytes = sectors * 512ULL;
        drv->block_dev.driver_data = drv;
        drv->block_dev.read_blocks = ReadBlocks;
        drv->block_dev.write_blocks = WriteBlocks;
        drv->active = true;

        g_drive_count++;
        StorageManager::RegisterDevice(&drv->block_dev);

        UART::Write("[AHCI] Registered ");
        UART::Write(drv->block_dev.name);
        UART::Write(" (");
        UART::WriteDec((int64_t)(drv->block_dev.geometry.total_bytes / (1024ULL * 1024ULL)));
        UART::Write(" MB)\n");
    }

    void Init() {
        g_drive_count = 0;
        uint32_t count = HAL::PCI::GetDeviceCount();

        for (uint32_t i = 0; i < count; i++) {
            HAL::PCIDevice dev = {};
            if (!HAL::PCI::GetDevice(i, &dev)) continue;

            // PCI Mass Storage (0x01) / SATA AHCI (0x06)
            if (dev.class_id == 0x01 && dev.subclass_id == 0x06) {
                uint16_t cmd = HAL::PCI::ConfigRead16(dev.bus, dev.device, dev.function, 0x04);
                cmd |= 0x06u; // Memory Space + Bus Master
                HAL::PCI::ConfigWrite32(dev.bus, dev.device, dev.function, 0x04, cmd);

                uint64_t abar = HAL::PCI::GetBAR64(dev, 5) & ~0xFULL;
                if (abar == 0) continue;

                // Map 8KB ABAR MMIO uncachable
                MMU::MapPage(abar, abar, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE);
                MMU::MapPage(abar + 4096, abar + 4096, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE);

                volatile HBAMem* hba = (volatile HBAMem*)abar;
                hba->ghc |= (1u << 31); // Enable AHCI mode

                UART::Write("[AHCI] Controller found BAR5=");
                UART::WriteHex(abar);
                UART::Write(" PI=");
                UART::WriteHex(hba->pi);
                UART::Write("\n");

                uint32_t pi = hba->pi;
                for (uint32_t p = 0; p < 32; p++) {
                    if (pi & (1u << p)) {
                        SetupPort(&hba->ports[p]);
                    }
                }
            }
        }
    }

    uint32_t GetDriveCount() { return g_drive_count; }
}
