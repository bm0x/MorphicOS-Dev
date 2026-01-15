#pragma once
#include <stdint.h>

namespace HAL {

    struct PCIDevice {
        uint8_t bus;
        uint8_t device;
        uint8_t function;
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t class_id;
        uint8_t subclass_id;
        uint8_t prog_if;
        uint8_t revision_id;
    };

    class PCI {
    public:
        static void Init();
        static void ScanBus(uint8_t bus);
        static void CheckDevice(uint8_t bus, uint8_t device);
        static void CheckFunction(uint8_t bus, uint8_t device, uint8_t function);

        // Configuration Space Access
        static uint32_t ConfigRead32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
        static uint16_t ConfigRead16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
        static uint8_t ConfigRead8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
        
        static void ConfigWrite32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

        // Helper to find a specific device (returns true if found)
        static bool FindDevice(uint16_t vendor_id, uint16_t device_id, PCIDevice* out_dev);
        
        // Get BAR (Base Address Register)
        static uint32_t GetBAR(PCIDevice dev, uint8_t bar_index);
    };

}
