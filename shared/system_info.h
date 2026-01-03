#pragma once

#include <stdint.h>

// Shared kernel <-> userspace data structures

struct MorphicDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid; // 1 if RTC read succeeded
    uint8_t reserved0;
} __attribute__((packed));

struct MorphicSystemInfo {
    char cpu_vendor[13]; // null-terminated
    char cpu_brand[49];  // null-terminated (may be empty)

    uint64_t total_mem_bytes;
    uint64_t free_mem_bytes;

    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
} __attribute__((packed));
