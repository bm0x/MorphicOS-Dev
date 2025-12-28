#include "video_device.h"
#include "early_term.h"

namespace VideoManager {
    static IVideoDevice* primaryDevice = nullptr;
    
    void Init() {
        primaryDevice = nullptr;
        EarlyTerm::Print("[HAL] Video Manager Initialized.\n");
    }
    
    void RegisterDevice(IVideoDevice* device) {
        if (!device) return;
        
        // First device becomes primary
        if (!primaryDevice) {
            primaryDevice = device;
            EarlyTerm::Print("[HAL] Video: ");
            EarlyTerm::Print(device->name);
            EarlyTerm::Print(" (");
            EarlyTerm::PrintDec(device->width);
            EarlyTerm::Print("x");
            EarlyTerm::PrintDec(device->height);
            EarlyTerm::Print(") registered.\n");
        }
    }
    
    IVideoDevice* GetPrimary() {
        return primaryDevice;
    }
    
    void PutPixel(uint32_t x, uint32_t y, uint32_t color) {
        if (primaryDevice && primaryDevice->put_pixel) {
            primaryDevice->put_pixel(x, y, color);
        }
    }
    
    void FillRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
        if (primaryDevice && primaryDevice->fill_rect) {
            primaryDevice->fill_rect(x, y, w, h, color);
        }
    }
    
    void Clear(uint32_t color) {
        if (primaryDevice && primaryDevice->clear) {
            primaryDevice->clear(color);
        }
    }
    
    uint32_t GetWidth() {
        return primaryDevice ? primaryDevice->width : 0;
    }
    
    uint32_t GetHeight() {
        return primaryDevice ? primaryDevice->height : 0;
    }
}
