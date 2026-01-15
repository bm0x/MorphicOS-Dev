#include "pci.h"
#include "../../video/verbose.h"
#include "io.h"
#include "../../../utils/std.h"

// PCI Configuration Ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

namespace HAL {

    // Helper: Simple Hex to String
    static void HexToString(uint32_t val, char* out, int bytes) {
        const char* hex = "0123456789ABCDEF";
        int nibbles = bytes * 2;
        out[0] = '0'; out[1] = 'x';
        for (int i = 0; i < nibbles; i++) {
            out[2 + i] = hex[(val >> ((nibbles - 1 - i) * 4)) & 0xF];
        }
        out[2 + nibbles] = '\0';
    }

    // Helper to calculate address for CONFIG_ADDRESS
    static uint32_t PciAddress(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        return (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    }

    uint32_t PCI::ConfigRead32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        IO::outl(PCI_CONFIG_ADDRESS, PciAddress(bus, slot, func, offset));
        return IO::inl(PCI_CONFIG_DATA);
    }

    uint16_t PCI::ConfigRead16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        IO::outl(PCI_CONFIG_ADDRESS, PciAddress(bus, slot, func, offset));
        return (uint16_t)((IO::inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
    }

    uint8_t PCI::ConfigRead8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
        IO::outl(PCI_CONFIG_ADDRESS, PciAddress(bus, slot, func, offset));
        return (uint8_t)((IO::inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
    }

    void PCI::ConfigWrite32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
        IO::outl(PCI_CONFIG_ADDRESS, PciAddress(bus, slot, func, offset));
        IO::outl(PCI_CONFIG_DATA, val);
    }

    void PCI::CheckFunction(uint8_t bus, uint8_t device, uint8_t function) {
        uint8_t baseClass;
        uint8_t subClass;
        uint8_t secondaryBus;

        baseClass = ConfigRead8(bus, device, function, 0x0B);
        subClass = ConfigRead8(bus, device, function, 0x0A);

        uint16_t vendor = ConfigRead16(bus, device, function, 0x00);
        uint16_t devID = ConfigRead16(bus, device, function, 0x02);

        char msg[128];
        char sVendor[10]; HexToString(vendor, sVendor, 2);
        char sDev[10]; HexToString(devID, sDev, 2);
        char sClass[10]; HexToString(baseClass, sClass, 1);
        
        // Manual string concat since no sprintf
        char* p = msg;
        kmemcpy(p, "FOUND ", 6); p += 6;
        kmemcpy(p, sVendor, 6); p += 6; // 0xXXXX
        *p++ = ':';
        kmemcpy(p, sDev, 6); p += 6;
        kmemcpy(p, " Class ", 7); p += 7;
        kmemcpy(p, sClass, 4); p += 4;
        *p = '\0';

        Verbose::Info("PCI", msg);

        if (baseClass == 0x06 && subClass == 0x04) {
            // PCI-to-PCI Bridge
            secondaryBus = ConfigRead8(bus, device, function, 0x19);
            ScanBus(secondaryBus);
        }
    }

    void PCI::CheckDevice(uint8_t bus, uint8_t device) {
        uint8_t function = 0;
        uint16_t vendorID = ConfigRead16(bus, device, function, 0x00);

        if (vendorID == 0xFFFF) return; // Device doesn't exist

        CheckFunction(bus, device, function);
        
        // Check for multi-function device
        uint8_t headerType = ConfigRead8(bus, device, function, 0x0E);
        if ((headerType & 0x80) != 0) {
            // It is a multi-function device, so check remaining functions
            for (function = 1; function < 8; function++) {
                if (ConfigRead16(bus, device, function, 0x00) != 0xFFFF) {
                    CheckFunction(bus, device, function);
                }
            }
        }
    }

    void PCI::ScanBus(uint8_t bus) {
        // Verbose::Info("PCI", "Scanning Bus...");
        for (uint8_t device = 0; device < 32; device++) {
            CheckDevice(bus, device);
        }
    }

    void PCI::Init() {
        Verbose::Info("PCI", "Initializing Subsystem...");
        // Check if Host Bridge exists at 0:0:0
        if (ConfigRead16(0, 0, 0, 0) == 0xFFFF) {
             Verbose::Error("PCI", "No Host Bridge found.");
             return;
        }
        ScanBus(0);
    }
    
    // Find a specific device
    bool PCI::FindDevice(uint16_t vendor_id, uint16_t device_id, PCIDevice* out_dev) {
        for (uint16_t bus = 0; bus < 256; bus++) {
            for (uint8_t dev = 0; dev < 32; dev++) {
                uint16_t v = ConfigRead16(bus, dev, 0, 0x00);
                if (v == 0xFFFF) continue;
                
                // Check Function 0
                uint16_t d = ConfigRead16(bus, dev, 0, 0x02);
                if (v == vendor_id && d == device_id) {
                    if (out_dev) {
                        out_dev->bus = (uint8_t)bus;
                        out_dev->device = dev;
                        out_dev->function = 0;
                    }
                    return true;
                }
                
                // Multi-function check logic implied for now
            }
        }
        return false;
    }
    
    uint32_t PCI::GetBAR(PCIDevice dev, uint8_t bar_index) {
        // BAR0 is offset 0x10, BAR1 0x14, etc.
        if (bar_index > 5) return 0;
        uint8_t offset = 0x10 + (bar_index * 4);
        return ConfigRead32(dev.bus, dev.device, dev.function, offset);
    }

}
