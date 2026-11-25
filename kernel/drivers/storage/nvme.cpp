#include "nvme.h"
#include "../../hal/arch/x86_64/pci.h"
#include "../../arch/common/mmu.h"
#include "../../hal/storage/block_device.h"
#include "../../hal/serial/uart.h"
#include "../../mm/pmm.h"
#include "../../utils/std.h"

namespace NVMe {

    struct NVMeCmd {
        uint8_t  opcode;
        uint8_t  flags;
        uint16_t cid;
        uint32_t nsid;
        uint64_t rsv0;
        uint64_t mptr;
        uint64_t prp1;
        uint64_t prp2;
        uint32_t cdw10;
        uint32_t cdw11;
        uint32_t cdw12;
        uint32_t cdw13;
        uint32_t cdw14;
        uint32_t cdw15;
    };

    struct NVMeCqe {
        uint32_t result;
        uint32_t rsv;
        uint16_t sq_head;
        uint16_t sq_id;
        uint16_t cid;
        uint16_t status;
    };

    struct NVMeController {
        volatile uint8_t* mmio;
        uint32_t          dstrd_bytes;

        NVMeCmd* admin_sq;
        NVMeCqe* admin_cq;
        uint16_t admin_sq_tail;
        uint16_t admin_cq_head;
        uint8_t  admin_phase;

        NVMeCmd* io_sq;
        NVMeCqe* io_cq;
        uint16_t io_sq_tail;
        uint16_t io_cq_head;
        uint8_t  io_phase;

        void*    dma_buf; // 4KB DMA bounce page
        uint16_t next_cid;
        uint32_t sector_size;
        uint64_t total_sectors;

        IBlockDevice block_dev;
        bool active;
    };

    static constexpr uint32_t MAX_NVME_DRIVES = 4;
    static constexpr uint16_t QUEUE_DEPTH     = 16;

    static NVMeController g_ctrls[MAX_NVME_DRIVES];
    static uint32_t       g_drive_count = 0;

    static inline uint32_t RegRead32(volatile uint8_t* base, uint32_t off) {
        return *(volatile uint32_t*)(base + off);
    }
    static inline uint64_t RegRead64(volatile uint8_t* base, uint32_t off) {
        return *(volatile uint64_t*)(base + off);
    }
    static inline void RegWrite32(volatile uint8_t* base, uint32_t off, uint32_t val) {
        *(volatile uint32_t*)(base + off) = val;
    }
    static inline void RegWrite64(volatile uint8_t* base, uint32_t off, uint64_t val) {
        *(volatile uint64_t*)(base + off) = val;
    }

    static bool SubmitAndPoll(NVMeController* c, bool isAdmin, const NVMeCmd& inCmd) {
        NVMeCmd* sq = isAdmin ? c->admin_sq : c->io_sq;
        NVMeCqe* cq = isAdmin ? c->admin_cq : c->io_cq;
        uint16_t& sqTail = isAdmin ? c->admin_sq_tail : c->io_sq_tail;
        uint16_t& cqHead = isAdmin ? c->admin_cq_head : c->io_cq_head;
        uint8_t&  phase  = isAdmin ? c->admin_phase   : c->io_phase;
        uint32_t  qid    = isAdmin ? 0u : 1u;

        NVMeCmd cmd = inCmd;
        cmd.cid = ++c->next_cid;
        sq[sqTail] = cmd;
        sqTail = (uint16_t)((sqTail + 1u) % QUEUE_DEPTH);

        __asm__ volatile("mfence" ::: "memory");

        // Ring SQ Tail Doorbell: 0x1000 + (2 * qid) * dstrd_bytes
        uint32_t sqDb = 0x1000u + (2u * qid) * c->dstrd_bytes;
        RegWrite32(c->mmio, sqDb, sqTail);

        // Poll CQ for completion with matching phase bit
        for (uint32_t spin = 0; spin < 3000000; spin++) {
            volatile NVMeCqe* entry = &cq[cqHead];
            uint16_t st = entry->status;
            if ((st & 1u) == phase) {
                uint16_t statusCode = (uint16_t)(st >> 1);
                cqHead = (uint16_t)((cqHead + 1u) % QUEUE_DEPTH);
                if (cqHead == 0) phase ^= 1u;

                uint32_t cqDb = 0x1000u + (2u * qid + 1u) * c->dstrd_bytes;
                RegWrite32(c->mmio, cqDb, cqHead);
                return (statusCode == 0);
            }
            __asm__ volatile("pause");
        }
        return false;
    }

    static bool ReadBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!dev || !dev->driver_data || !buffer || count == 0) return false;
        NVMeController* c = (NVMeController*)dev->driver_data;
        uint32_t secPerPage = 4096u / c->sector_size;
        if (secPerPage == 0) secPerPage = 1;
        uint8_t* dst = (uint8_t*)buffer;

        while (count > 0) {
            uint32_t chunk = (count > secPerPage) ? secPerPage : count;
            NVMeCmd cmd = {};
            cmd.opcode = 0x02; // NVM Read
            cmd.nsid   = 1;
            cmd.prp1   = (uint64_t)c->dma_buf;
            cmd.cdw10  = (uint32_t)(lba & 0xFFFFFFFFULL);
            cmd.cdw11  = (uint32_t)(lba >> 32);
            cmd.cdw12  = chunk - 1u;
            if (!SubmitAndPoll(c, false, cmd)) return false;

            kmemcpy(dst, c->dma_buf, (size_t)chunk * c->sector_size);
            dst += (size_t)chunk * c->sector_size;
            lba += chunk;
            count -= chunk;
        }
        return true;
    }

    static bool WriteBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        if (!dev || !dev->driver_data || !buffer || count == 0) return false;
        NVMeController* c = (NVMeController*)dev->driver_data;
        uint32_t secPerPage = 4096u / c->sector_size;
        if (secPerPage == 0) secPerPage = 1;
        const uint8_t* src = (const uint8_t*)buffer;

        while (count > 0) {
            uint32_t chunk = (count > secPerPage) ? secPerPage : count;
            kmemcpy(c->dma_buf, src, (size_t)chunk * c->sector_size);

            NVMeCmd cmd = {};
            cmd.opcode = 0x01; // NVM Write
            cmd.nsid   = 1;
            cmd.prp1   = (uint64_t)c->dma_buf;
            cmd.cdw10  = (uint32_t)(lba & 0xFFFFFFFFULL);
            cmd.cdw11  = (uint32_t)(lba >> 32);
            cmd.cdw12  = chunk - 1u;
            if (!SubmitAndPoll(c, false, cmd)) return false;

            src += (size_t)chunk * c->sector_size;
            lba += chunk;
            count -= chunk;
        }
        return true;
    }

    static void InitController(const HAL::PCIDevice& dev) {
        if (g_drive_count >= MAX_NVME_DRIVES) return;

        uint16_t pciCmd = HAL::PCI::ConfigRead16(dev.bus, dev.device, dev.function, 0x04);
        pciCmd |= 0x06u; // Memory Space + Bus Master
        HAL::PCI::ConfigWrite32(dev.bus, dev.device, dev.function, 0x04, pciCmd);

        uint64_t bar0 = HAL::PCI::GetBAR64(dev, 0) & ~0xFULL;
        if (bar0 == 0) return;

        // Map 16KB BAR0 MMIO uncachable
        for (uint64_t off = 0; off < 16384; off += 4096) {
            MMU::MapPage(bar0 + off, bar0 + off, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NOCACHE);
        }

        NVMeController* c = &g_ctrls[g_drive_count];
        kmemset(c, 0, sizeof(NVMeController));
        c->mmio = (volatile uint8_t*)bar0;

        uint64_t cap = RegRead64(c->mmio, 0x00);
        uint32_t dstrd = (uint32_t)((cap >> 32) & 0x0FULL);
        c->dstrd_bytes = 4u << dstrd;

        // 1. Disable controller and wait for CSTS.RDY == 0
        RegWrite32(c->mmio, 0x14, RegRead32(c->mmio, 0x14) & ~1u);
        for (uint32_t i = 0; i < 2000000; i++) {
            if ((RegRead32(c->mmio, 0x1C) & 1u) == 0) break;
            __asm__ volatile("pause");
        }

        // 2. Allocate DMA queue pages via PMM
        c->admin_sq = (NVMeCmd*)PMM::AllocPage();
        c->admin_cq = (NVMeCqe*)PMM::AllocPage();
        c->io_sq    = (NVMeCmd*)PMM::AllocPage();
        c->io_cq    = (NVMeCqe*)PMM::AllocPage();
        c->dma_buf  = PMM::AllocPage();
        if (!c->admin_sq || !c->admin_cq || !c->io_sq || !c->io_cq || !c->dma_buf) return;

        kmemset(c->admin_sq, 0, 4096);
        kmemset(c->admin_cq, 0, 4096);
        kmemset(c->io_sq, 0, 4096);
        kmemset(c->io_cq, 0, 4096);
        kmemset(c->dma_buf, 0, 4096);
        c->admin_phase = 1;
        c->io_phase    = 1;

        // 3. Configure Admin Queue Attributes (AQA), ASQ, ACQ
        uint32_t aqa = ((uint32_t)(QUEUE_DEPTH - 1u) << 16) | (uint32_t)(QUEUE_DEPTH - 1u);
        RegWrite32(c->mmio, 0x24, aqa);
        RegWrite64(c->mmio, 0x28, (uint64_t)c->admin_sq);
        RegWrite64(c->mmio, 0x30, (uint64_t)c->admin_cq);

        // 4. Enable controller: IOSQES=6 (64B), IOCQES=4 (16B), MPS=0 (4KB), EN=1
        RegWrite32(c->mmio, 0x14, (4u << 20) | (6u << 16) | 1u);
        bool ready = false;
        for (uint32_t i = 0; i < 3000000; i++) {
            if (RegRead32(c->mmio, 0x1C) & 1u) { ready = true; break; }
            __asm__ volatile("pause");
        }
        if (!ready) return;

        // 5. Identify Namespace 1 (opcode 0x06, CNS=0)
        NVMeCmd idNs = {};
        idNs.opcode = 0x06;
        idNs.nsid   = 1;
        idNs.prp1   = (uint64_t)c->dma_buf;
        idNs.cdw10  = 0;
        if (!SubmitAndPoll(c, true, idNs)) return;

        uint64_t nsze = *(uint64_t*)c->dma_buf;
        uint8_t flbas = ((uint8_t*)c->dma_buf)[26] & 0x0F;
        uint32_t lbaf = *(uint32_t*)(((uint8_t*)c->dma_buf) + 128 + flbas * 4);
        uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFF);
        c->sector_size = (lbads >= 9 && lbads <= 12) ? (1u << lbads) : 512u;
        c->total_sectors = nsze;
        if (nsze == 0) return;

        // 6. Create I/O Completion Queue 1 (opcode 0x05)
        NVMeCmd mkCq = {};
        mkCq.opcode = 0x05;
        mkCq.prp1   = (uint64_t)c->io_cq;
        mkCq.cdw10  = ((uint32_t)(QUEUE_DEPTH - 1u) << 16) | 1u;
        mkCq.cdw11  = 1u; // Physically contiguous
        if (!SubmitAndPoll(c, true, mkCq)) return;

        // 7. Create I/O Submission Queue 1 (opcode 0x01)
        NVMeCmd mkSq = {};
        mkSq.opcode = 0x01;
        mkSq.prp1   = (uint64_t)c->io_sq;
        mkSq.cdw10  = ((uint32_t)(QUEUE_DEPTH - 1u) << 16) | 1u;
        mkSq.cdw11  = (1u << 16) | 1u; // CQID=1, Physically contiguous
        if (!SubmitAndPoll(c, true, mkSq)) return;

        c->block_dev.name[0] = 'n';
        c->block_dev.name[1] = 'v';
        c->block_dev.name[2] = 'm';
        c->block_dev.name[3] = 'e';
        c->block_dev.name[4] = (char)('0' + g_drive_count);
        c->block_dev.name[5] = '\0';

        c->block_dev.geometry.sector_size = c->sector_size;
        c->block_dev.geometry.total_sectors = c->total_sectors;
        c->block_dev.geometry.total_bytes = c->total_sectors * (uint64_t)c->sector_size;
        c->block_dev.driver_data = c;
        c->block_dev.read_blocks = ReadBlocks;
        c->block_dev.write_blocks = WriteBlocks;
        c->active = true;

        g_drive_count++;
        StorageManager::RegisterDevice(&c->block_dev);

        UART::Write("[NVMe] Registered ");
        UART::Write(c->block_dev.name);
        UART::Write(" (");
        UART::WriteDec((int64_t)(c->block_dev.geometry.total_bytes / (1024ULL * 1024ULL)));
        UART::Write(" MB, ");
        UART::WriteDec((int64_t)c->sector_size);
        UART::Write("B/sector)\n");
    }

    void Init() {
        g_drive_count = 0;
        uint32_t count = HAL::PCI::GetDeviceCount();

        for (uint32_t i = 0; i < count; i++) {
            HAL::PCIDevice dev = {};
            if (!HAL::PCI::GetDevice(i, &dev)) continue;

            // PCI Mass Storage (0x01) / Non-Volatile Memory (0x08) / NVMe (0x02)
            if (dev.class_id == 0x01 && dev.subclass_id == 0x08 && dev.prog_if == 0x02) {
                UART::Write("[NVMe] Controller found on PCI\n");
                InitController(dev);
            }
        }
    }

    uint32_t GetDriveCount() { return g_drive_count; }
}
