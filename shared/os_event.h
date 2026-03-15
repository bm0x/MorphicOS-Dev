#pragma once
#include <stdint.h>

struct OSEvent {
    enum Type {
        NONE = 0,
        MOUSE_MOVE = 1,
        MOUSE_CLICK = 2,
        KEY_PRESS = 3,
        KEY_RELEASE = 4,
        WINDOW_CREATED = 5,
        WINDOW_DESTROYED = 6,
        USER_MESSAGE = 64
    };

    uint32_t type;
    int32_t dx;
    int32_t dy;
    uint32_t buttons;  // Mouse buttons or Key modifiers
    uint32_t scancode; // Keyboard scancode
    uint32_t ascii;    // ASCII character
};
