#pragma once
#include <stdint.h>

struct MorphicNetInfo {
    uint32_t present;
    uint32_t link_up;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  mac[6];
    uint16_t reserved;
    uint64_t tx_packets;
    uint64_t rx_packets;
    char     driver_name[24];
};
