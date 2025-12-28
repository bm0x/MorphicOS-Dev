#pragma once

#include <stdint.h>

// Device Types
enum class DeviceType {
    INPUT,
    VIDEO,
    STORAGE,
    NETWORK,
    UNKNOWN
};

// Forward declarations
struct IInputDevice;
struct IVideoDevice;

// Device Registry - Central hub for driver registration
// Drivers call Register() during init to announce themselves
namespace DeviceRegistry {
    void Init();
    
    // Register a device with the system
    void Register(DeviceType type, void* device);
    
    // Get primary devices
    IInputDevice* GetPrimaryInput();
    IVideoDevice* GetPrimaryVideo();
    
    // Device enumeration
    uint32_t GetDeviceCount(DeviceType type);
}
