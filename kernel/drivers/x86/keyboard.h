#pragma once
#include <stdint.h>

namespace Keyboard {
    void Init();
    void ProcessByte(uint8_t scancode);
    char GetChar();
}
