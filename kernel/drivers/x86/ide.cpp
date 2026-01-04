#include "ide.h"
#include "../../hal/arch/x86_64/io.h"
#include "../../hal/device_registry.h"
#include "../../hal/serial/uart.h"
// #include "../../hal/storage/storage_manager.h" // Use DeviceRegistry instead
#include "../../hal/video/early_term.h"
#include "../../utils/std.h"

namespace IDE {

    // IDE Registers (Offset from Base)
    #define ATA_REG_DATA        0x00
    #define ATA_REG_ERROR       0x01
    #define ATA_REG_FEATURES    0x01
    #define ATA_REG_SECCOUNT0   0x02
    #define ATA_REG_LBA0        0x03
    #define ATA_REG_LBA1        0x04
    #define ATA_REG_LBA2        0x05
    #define ATA_REG_HDDEVSEL    0x06
    #define ATA_REG_COMMAND     0x07
    #define ATA_REG_STATUS      0x07

    #define ATA_CMD_READ_PIO    0x20
    #define ATA_CMD_WRITE_PIO   0x30
    #define ATA_CMD_CACHE_FLUSH 0xE7
    #define ATA_CMD_IDENTIFY    0xEC

    struct IDEDevice : public IBlockDevice {
        uint16_t base;   // I/O Base (e.g. 0x1F0)
        uint16_t ctrl;   // Control Base (e.g. 0x3F6)
        bool slave;      // Master/Slave
    };

    static IDEDevice channels[4];
    static int channelCount = 0;

    static void IDE_Wait(uint16_t base) {
        for (int i = 0; i < 4; i++) IO::inb(base + ATA_REG_STATUS);
    }

    static bool IDE_Poll(uint16_t base) {
        // Wait for BSY to be 0
        for (int i = 0; i < 100000; i++) {
            uint8_t status = IO::inb(base + ATA_REG_STATUS);
            if ((status & 0x80) == 0) { // BSY clear
                if (status & 0x08) return true; // DRQ set
                if (status & 0x01) return false; // ERR set
                if (status & 0x20) return false; // DF set
            }
        }
        return false;
    }

    static bool ReadBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        IDEDevice* ide = (IDEDevice*)dev;
        uint16_t base = ide->base;
        uint8_t* buf = (uint8_t*)buffer;

        // Limit to 28-bit LBA for simplicity (max 128GB)
        if (lba > 0x0FFFFFFF) return false;

        for (uint32_t i = 0; i < count; i++) {
            IDE_Wait(base);
            
            // Select Drive + LBA High 4 bits
            IO::outb(base + ATA_REG_HDDEVSEL, 0xE0 | (ide->slave << 4) | ((lba >> 24) & 0x0F));
            IO::outb(base + ATA_REG_FEATURES, 0x00); // PIO mode
            IO::outb(base + ATA_REG_SECCOUNT0, 1);
            IO::outb(base + ATA_REG_LBA0, (uint8_t)lba);
            IO::outb(base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
            IO::outb(base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
            IO::outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

            if (!IDE_Poll(base)) return false;

            // Read 256 words (512 bytes)
            for (int j = 0; j < 256; j++) {
                uint16_t data = IO::inw(base + ATA_REG_DATA);
                *(uint16_t*)(buf + j * 2) = data;
            }
            
            buf += 512;
            lba++;
        }
        return true;
    }

    static bool WriteBlocks(IBlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
        IDEDevice* ide = (IDEDevice*)dev;
        uint16_t base = ide->base;
        uint8_t* buf = (uint8_t*)buffer;

        if (lba > 0x0FFFFFFF) return false;

        for (uint32_t i = 0; i < count; i++) {
            IDE_Wait(base);
            
            IO::outb(base + ATA_REG_HDDEVSEL, 0xE0 | (ide->slave << 4) | ((lba >> 24) & 0x0F));
            IO::outb(base + ATA_REG_FEATURES, 0x00);
            IO::outb(base + ATA_REG_SECCOUNT0, 1);
            IO::outb(base + ATA_REG_LBA0, (uint8_t)lba);
            IO::outb(base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
            IO::outb(base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
            IO::outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

            if (!IDE_Poll(base)) return false;

            for (int j = 0; j < 256; j++) {
                IO::outw(base + ATA_REG_DATA, *(uint16_t*)(buf + j * 2));
            }
            
            IO::outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
            IDE_Wait(base);
            
            buf += 512;
            lba++;
        }
        return true;
    }

    static void Probe(uint16_t base, uint16_t ctrl, bool slave, const char* name) {
        // Soft Reset
        IO::outb(ctrl, 0x04);
        IDE_Wait(base);
        IO::outb(ctrl, 0x00);
        IDE_Wait(base);

        // Select Drive
        IO::outb(base + ATA_REG_HDDEVSEL, 0xA0 | (slave << 4));
        IDE_Wait(base);

        // Send Identify
        IO::outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
        IDE_Wait(base);

        uint8_t status = IO::inb(base + ATA_REG_STATUS);
        if (status == 0) return; // No device

        // Check if it's ATA (not ATAPI)
        // Wait for BSY to clear
        int timeout = 10000;
        while ((IO::inb(base + ATA_REG_STATUS) & 0x80) && timeout-- > 0);

        if (IO::inb(base + ATA_REG_LBA1) != 0 || IO::inb(base + ATA_REG_LBA2) != 0) {
            // Not ATA (maybe ATAPI)
            return;
        }

        // Read Identify Data (discard for now, just to clear buffer)
        for (int i = 0; i < 256; i++) IO::inw(base + ATA_REG_DATA);

        // Register Device
        IDEDevice* dev = &channels[channelCount++];
        dev->base = base;
        dev->ctrl = ctrl;
        dev->slave = slave;
        
        // Setup IBlockDevice
        // kstrcpy(dev->name, name);
        int n = 0;
        while(name[n] && n < 31) { dev->name[n] = name[n]; n++; }
        dev->name[n] = 0;

        dev->geometry.sector_size = 512;
        dev->geometry.total_bytes = 2ULL * 1024 * 1024 * 1024; // Assume 2GB for now (should read from Identify)
        dev->read_blocks = ReadBlocks;
        dev->write_blocks = WriteBlocks;

        UART::Write("[IDE] Found device: ");
        UART::Write(name);
        UART::Write("\n");

        DeviceRegistry::Register(DeviceType::STORAGE, dev);
    }

    void Init() {
        EarlyTerm::Print("[IDE] Probing devices...\n");
        UART::Write("[IDE] Probing devices...\n");
        
        // Primary Master (hda)
        Probe(0x1F0, 0x3F6, false, "hda");
        // Primary Slave (hdb)
        Probe(0x1F0, 0x3F6, true, "hdb");
        // Secondary Master (hdc)
        Probe(0x170, 0x376, false, "hdc");
        // Secondary Slave (hdd)
        Probe(0x170, 0x376, true, "hdd");
    }
}
