#pragma once
#include <stdint.h>

namespace xHCI {
    void Init();
    void PollEvents();
    uint32_t GetControllerCount();
    uint32_t GetConnectedPortCount();
}
