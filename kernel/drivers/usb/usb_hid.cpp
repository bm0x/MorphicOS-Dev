#include "usb_hid.h"
#include "../../../shared/os_event.h"

namespace InputManager {
    void PushEvent(const OSEvent& ev);
}

namespace USB_HID {

    static uint8_t g_prev_keys[6] = {0};
    static uint8_t g_prev_buttons = 0;

    static char HidUsageToAscii(uint8_t usage, bool shift) {
        if (usage >= 0x04 && usage <= 0x1D) {
            return (char)((shift ? 'A' : 'a') + (usage - 0x04));
        }
        static const char kNumNormal[10] = {'1','2','3','4','5','6','7','8','9','0'};
        static const char kNumShift[10]  = {'!','@','#','$','%','^','&','*','(',')'};
        if (usage >= 0x1E && usage <= 0x27) {
            return shift ? kNumShift[usage - 0x1E] : kNumNormal[usage - 0x1E];
        }
        switch (usage) {
            case 0x28: return '\n'; // Enter
            case 0x29: return 27;   // Escape
            case 0x2A: return '\b'; // Backspace
            case 0x2B: return '\t'; // Tab
            case 0x2C: return ' ';  // Space
            case 0x2D: return shift ? '_' : '-';
            case 0x2E: return shift ? '+' : '=';
            case 0x2F: return shift ? '{' : '[';
            case 0x30: return shift ? '}' : ']';
            case 0x31: return shift ? '|' : '\\';
            case 0x33: return shift ? ':' : ';';
            case 0x34: return shift ? '"' : '\'';
            case 0x36: return shift ? '<' : ',';
            case 0x37: return shift ? '>' : '.';
            case 0x38: return shift ? '?' : '/';
            default:   return 0;
        }
    }

    void ResetState() {
        for (int i = 0; i < 6; i++) g_prev_keys[i] = 0;
        g_prev_buttons = 0;
    }

    void ParseBootKeyboardReport(const uint8_t report[8]) {
        if (!report) return;
        uint8_t mods = report[0];
        bool shift = (mods & 0x22u) != 0; // Left or Right Shift

        for (int i = 2; i < 8; i++) {
            uint8_t code = report[i];
            if (code == 0 || code == 1) continue;

            bool alreadyDown = false;
            for (int j = 0; j < 6; j++) {
                if (g_prev_keys[j] == code) {
                    alreadyDown = true;
                    break;
                }
            }
            if (!alreadyDown) {
                char ch = HidUsageToAscii(code, shift);
                if (ch != 0) {
                    OSEvent ev = {};
                    ev.type = OSEvent::KEY_PRESS;
                    ev.ascii = ch;
                    ev.scancode = code;
                    InputManager::PushEvent(ev);
                }
            }
        }

        for (int i = 0; i < 6; i++) g_prev_keys[i] = report[i + 2];
    }

    void ParseBootMouseReport(const uint8_t report[4]) {
        if (!report) return;
        uint8_t buttons = report[0] & 0x07u;
        int8_t dx = (int8_t)report[1];
        int8_t dy = (int8_t)report[2];

        if (dx != 0 || dy != 0) {
            OSEvent ev = {};
            ev.type = OSEvent::MOUSE_MOVE;
            ev.dx = (int32_t)dx;
            ev.dy = (int32_t)dy;
            ev.buttons = buttons;
            InputManager::PushEvent(ev);
        }

        if (buttons != g_prev_buttons) {
            OSEvent ev = {};
            ev.type = OSEvent::MOUSE_CLICK;
            ev.dx = 0;
            ev.dy = 0;
            ev.buttons = buttons;
            InputManager::PushEvent(ev);
            g_prev_buttons = buttons;
        }
    }

}
