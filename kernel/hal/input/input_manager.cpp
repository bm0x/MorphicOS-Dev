#include "input_device.h"
#include "../video/early_term.h"

namespace InputManager {
    // Maximum registered input devices
    static const int MAX_INPUT_DEVICES = 8;
    
    static IInputDevice* devices[MAX_INPUT_DEVICES];
    static uint32_t deviceCount = 0;
    
    void Init() {
        for (int i = 0; i < MAX_INPUT_DEVICES; i++) {
            devices[i] = nullptr;
        }
        deviceCount = 0;
        EarlyTerm::Print("[HAL] Input Manager Initialized.\n");
    }
    
    void RegisterDevice(IInputDevice* device) {
        if (!device) return;
        if (deviceCount >= MAX_INPUT_DEVICES) {
            EarlyTerm::Print("[HAL] ERROR: Max input devices reached!\n");
            return;
        }
        
        devices[deviceCount++] = device;
        
        // Initialize device
        if (device->init) {
            device->init();
        }
        
        EarlyTerm::Print("[HAL] Input: ");
        EarlyTerm::Print(device->name);
        EarlyTerm::Print(" registered.\n");
    }
    
    bool PollEvent(InputEvent* outEvent) {
        if (!outEvent) return false;
        
        // Poll all devices in priority order
        for (uint32_t i = 0; i < deviceCount; i++) {
            if (devices[i] && devices[i]->poll_event) {
                if (devices[i]->poll_event(outEvent)) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    uint32_t GetDeviceCount() {
        return deviceCount;
    }
    
    // Called by IRQ dispatcher to notify all input devices
    void DispatchInterrupt(uint32_t irq) {
        // IRQ1 = Keyboard, IRQ12 = Mouse (PS/2)
        for (uint32_t i = 0; i < deviceCount; i++) {
            if (devices[i] && devices[i]->on_interrupt) {
                devices[i]->on_interrupt();
            }
        }
    }
}
