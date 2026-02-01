#include "device_registry.h"
#include "input/input_device.h"
#include "input/evdev.h"
#include "video/video_device.h"
#include "storage/block_device.h"
#include "video/early_term.h"

namespace DeviceRegistry {
    void Init() {
        // Initialize evdev subsystem first (Linux-style event device)
        Evdev::Init();
        
        InputManager::Init();
        VideoManager::Init();
        StorageManager::Init();
        EarlyTerm::Print("[HAL] Device Registry Initialized.\n");
    }
    
    void Register(DeviceType type, void* device) {
        if (!device) return;
        
        switch (type) {
            case DeviceType::INPUT:
                InputManager::RegisterDevice((IInputDevice*)device);
                break;
            case DeviceType::VIDEO:
                VideoManager::RegisterDevice((IVideoDevice*)device);
                break;
            case DeviceType::STORAGE:
                StorageManager::RegisterDevice((IBlockDevice*)device);
                break;
            default:
                EarlyTerm::Print("[HAL] Unknown device type.\n");
                break;
        }
    }
    
    IInputDevice* GetPrimaryInput() {
        return nullptr;
    }
    
    IVideoDevice* GetPrimaryVideo() {
        return VideoManager::GetPrimary();
    }
    
    uint32_t GetDeviceCount(DeviceType type) {
        switch (type) {
            case DeviceType::INPUT:
                return InputManager::GetDeviceCount();
            case DeviceType::STORAGE:
                return StorageManager::GetDeviceCount();
            default:
                return 0;
        }
    }
}
