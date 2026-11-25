#include "pci.h"
#include "../../video/verbose.h"
#include "../../serial/uart.h"
#include "io.h"
#include "../../../utils/std.h"

// PCI Configuration Ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

namespace HAL {

    static constexpr uint32_t MAX_PCI_DEVICES = 64;
    static PCIDevice s_devices[MAX_PCI_DEVICES];
    static uint32_t s_deviceCount = 0;
    static PCIGpuInfo s_primaryGpu = {};
    static int s_primaryGpuScore = -1;

    static void CopyStr(char* dst, size_t maxLen, const char* src) {
        size_t i = 0;
        if (src) {
            while (src[i] && i < maxLen - 1) {
                dst[i] = src[i];
                i++;
            }
        }
        dst[i] = '\0';
    }

    static void HexToString(uint32_t val, char* out, int bytes) {
        const char* hex = "0123456789ABCDEF";
        int nibbles = bytes * 2;
        out[0] = '0'; out[1] = 'x';
        for (int i = 0; i < nibbles; i++) {
            out[2 + i] = hex[(val >> ((nibbles - 1 - i) * 4)) & 0xF];
        }
        out[2 + nibbles] = '\0';
    }

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

    static void ClassifyGpuVendor(uint16_t vendor, uint16_t devId, char vendorOut[24], char modelOut[64], int* outScore) {
        char hexId[10];
        HexToString(devId, hexId, 2);

        if (vendor == 0x10DE) {
            CopyStr(vendorOut, 24, "NVIDIA");
            CopyStr(modelOut, 64, "NVIDIA GeForce/RTX GPU (");
            *outScore = 100; // Prefer discrete NVIDIA GPU
        } else if (vendor == 0x1002 || vendor == 0x1022) {
            CopyStr(vendorOut, 24, "AMD");
            CopyStr(modelOut, 64, "AMD Radeon/RDNA GPU (");
            *outScore = 95; // Prefer discrete/APU AMD GPU
        } else if (vendor == 0x8086) {
            CopyStr(vendorOut, 24, "INTEL");
            CopyStr(modelOut, 64, "Intel Arc/Iris/UHD GPU (");
            *outScore = 80;
        } else if (vendor == 0x1AF4) {
            CopyStr(vendorOut, 24, "VIRTIO");
            CopyStr(modelOut, 64, "VirtIO Paravirtual GPU (");
            *outScore = 70;
        } else if (vendor == 0x15AD) {
            CopyStr(vendorOut, 24, "VMWARE");
            CopyStr(modelOut, 64, "VMware SVGA II Adapter (");
            *outScore = 65;
        } else if (vendor == 0x1234 || vendor == 0x80EE) {
            CopyStr(vendorOut, 24, "BOCHS/VBOX");
            CopyStr(modelOut, 64, "Standard VBE/BGA Display (");
            *outScore = 50;
        } else {
            CopyStr(vendorOut, 24, "PCI-GPU");
            CopyStr(modelOut, 64, "Generic PCI Display (");
            *outScore = 40;
        }

        size_t len = kstrlen(modelOut);
        for (size_t i = 0; hexId[i] && len < 61; i++) {
            modelOut[len++] = hexId[i];
        }
        if (len < 62) modelOut[len++] = ')';
        modelOut[len] = '\0';
    }

    uint64_t PCI::GetBAR64(PCIDevice dev, uint8_t bar_index) {
        if (bar_index > 5) return 0;
        uint32_t bar_low = GetBAR(dev, bar_index);
        if (bar_low == 0 || (bar_low & 0x01)) {
            // I/O space BAR or unallocated
            return (uint64_t)(bar_low & ~0x3U);
        }
        uint8_t mem_type = (bar_low >> 1) & 0x03;
        uint64_t phys = (uint64_t)(bar_low & ~0xFULL);
        if (mem_type == 0x02 && bar_index < 5) {
            uint32_t bar_high = GetBAR(dev, bar_index + 1);
            phys |= ((uint64_t)bar_high << 32);
        }
        return phys;
    }

    void PCI::CheckFunction(uint8_t bus, uint8_t device, uint8_t function) {
        uint8_t baseClass = ConfigRead8(bus, device, function, 0x0B);
        uint8_t subClass  = ConfigRead8(bus, device, function, 0x0A);
        uint8_t progIF    = ConfigRead8(bus, device, function, 0x09);
        uint8_t revID     = ConfigRead8(bus, device, function, 0x08);
        uint16_t vendor   = ConfigRead16(bus, device, function, 0x00);
        uint16_t devID    = ConfigRead16(bus, device, function, 0x02);

        PCIDevice rec = {bus, device, function, vendor, devID, baseClass, subClass, progIF, revID};
        if (s_deviceCount < MAX_PCI_DEVICES) {
            s_devices[s_deviceCount++] = rec;
        }

        char msg[128];
        char sVendor[10]; HexToString(vendor, sVendor, 2);
        char sDev[10]; HexToString(devID, sDev, 2);
        char sClass[10]; HexToString(baseClass, sClass, 1);

        char* p = msg;
        kmemcpy(p, "FOUND ", 6); p += 6;
        kmemcpy(p, sVendor, 6); p += 6;
        *p++ = ':';
        kmemcpy(p, sDev, 6); p += 6;
        kmemcpy(p, " Class ", 7); p += 7;
        kmemcpy(p, sClass, 4); p += 4;
        *p = '\0';

        Verbose::Info("PCI", msg);

        // Class 0x03 = Display Controller (0x00 VGA, 0x01 XGA, 0x02 3D/NVIDIA Compute, 0x80 Other Display)
        if (baseClass == 0x03) {
            // Ensure Memory Space (bit 1) + Bus Master (bit 2) are enabled on PCI command register
            uint32_t cmdStatus = ConfigRead32(bus, device, function, 0x04);
            if ((cmdStatus & 0x06) != 0x06) {
                ConfigWrite32(bus, device, function, 0x04, cmdStatus | 0x06);
            }

            char vName[24];
            char mName[64];
            int score = 0;
            ClassifyGpuVendor(vendor, devID, vName, mName, &score);

            if (score > s_primaryGpuScore) {
                s_primaryGpuScore = score;
                s_primaryGpu.present = true;
                s_primaryGpu.bus = bus;
                s_primaryGpu.slot = device;
                s_primaryGpu.func = function;
                s_primaryGpu.vendor_id = vendor;
                s_primaryGpu.device_id = devID;
                s_primaryGpu.subclass_id = subClass;
                s_primaryGpu.prog_if = progIF;
                s_primaryGpu.bar0_phys = GetBAR64(rec, 0);
                s_primaryGpu.bar1_phys = GetBAR64(rec, 1);
                CopyStr(s_primaryGpu.vendor_name, sizeof(s_primaryGpu.vendor_name), vName);
                CopyStr(s_primaryGpu.model_name, sizeof(s_primaryGpu.model_name), mName);

                UART::Write("[PCI-GPU] Selected Primary GPU: ");
                UART::Write(s_primaryGpu.model_name);
                UART::Write(" BAR0=0x");
                UART::WriteHex(s_primaryGpu.bar0_phys);
                UART::Write("\n");
            }
        }

        if (baseClass == 0x06 && subClass == 0x04) {
            uint8_t secondaryBus = ConfigRead8(bus, device, function, 0x19);
            if (secondaryBus != bus) {
                ScanBus(secondaryBus);
            }
        }
    }

    void PCI::CheckDevice(uint8_t bus, uint8_t device) {
        uint8_t function = 0;
        uint16_t vendorID = ConfigRead16(bus, device, function, 0x00);
        if (vendorID == 0xFFFF) return;

        CheckFunction(bus, device, function);

        uint8_t headerType = ConfigRead8(bus, device, function, 0x0E);
        if ((headerType & 0x80) != 0) {
            for (function = 1; function < 8; function++) {
                if (ConfigRead16(bus, device, function, 0x00) != 0xFFFF) {
                    CheckFunction(bus, device, function);
                }
            }
        }
    }

    void PCI::ScanBus(uint8_t bus) {
        for (uint8_t device = 0; device < 32; device++) {
            CheckDevice(bus, device);
        }
    }

    void PCI::Init() {
        s_deviceCount = 0;
        kmemset(&s_primaryGpu, 0, sizeof(s_primaryGpu));
        s_primaryGpuScore = -1;

        Verbose::Info("PCI", "Initializing Subsystem...");
        if (ConfigRead16(0, 0, 0, 0) == 0xFFFF) {
            Verbose::Error("PCI", "No Host Bridge found.");
            return;
        }

        uint8_t rootHeader = ConfigRead8(0, 0, 0, 0x0E);
        if ((rootHeader & 0x80) == 0) {
            ScanBus(0);
        } else {
            // Multiple PCI host controllers
            for (uint8_t fn = 0; fn < 8; fn++) {
                if (ConfigRead16(0, 0, fn, 0x00) == 0xFFFF) break;
                ScanBus(fn);
            }
        }
    }

    bool PCI::FindDevice(uint16_t vendor_id, uint16_t device_id, PCIDevice* out_dev) {
        for (uint32_t i = 0; i < s_deviceCount; i++) {
            if (s_devices[i].vendor_id == vendor_id && s_devices[i].device_id == device_id) {
                if (out_dev) *out_dev = s_devices[i];
                return true;
            }
        }
        return false;
    }

    uint32_t PCI::GetDeviceCount() {
        return s_deviceCount;
    }

    bool PCI::GetDevice(uint32_t index, PCIDevice* out_dev) {
        if (index >= s_deviceCount || !out_dev) return false;
        *out_dev = s_devices[index];
        return true;
    }

    bool PCI::GetPrimaryGPU(PCIGpuInfo* out_gpu) {
        if (!out_gpu) return false;
        *out_gpu = s_primaryGpu;
        return s_primaryGpu.present;
    }

    uint32_t PCI::GetBAR(PCIDevice dev, uint8_t bar_index) {
        if (bar_index > 5) return 0;
        uint8_t offset = 0x10 + (bar_index * 4);
        return ConfigRead32(dev.bus, dev.device, dev.function, offset);
    }

}
