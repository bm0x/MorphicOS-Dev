#pragma once
#include <stdint.h>
#include "../../../shared/net_uapi.h"

namespace NetE1000 {
    void Init();
    bool IsPresent();
    bool IsLinkUp();
    bool GetInfo(MorphicNetInfo* out);
    bool SendPacket(const void* data, uint16_t length);
}
