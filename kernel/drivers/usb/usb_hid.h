#pragma once
#include <stdint.h>

namespace USB_HID {
    // Parse standard 8-byte USB HID Boot Keyboard report and inject OSEvent::KEY_PRESS
    void ParseBootKeyboardReport(const uint8_t report[8]);

    // Parse standard 3/4-byte USB HID Boot Mouse report and inject OSEvent::MOUSE_MOVE / CLICK
    void ParseBootMouseReport(const uint8_t report[4]);

    void ResetState();
}
