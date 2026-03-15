#pragma once

#include <stdint.h>

// Graphics UAPI version
#define GRAPHICS_UAPI_VERSION_MAJOR 1
#define GRAPHICS_UAPI_VERSION_MINOR 0

// Supported pixel formats
enum GraphicsUapiPixelFormat : uint32_t {
    GRAPHICS_FORMAT_ARGB8888 = 0,
    GRAPHICS_FORMAT_XRGB8888 = 1,
    GRAPHICS_FORMAT_RGB888 = 2,
};

#define GRAPHICS_FORMAT_BIT_ARGB8888 (1u << GRAPHICS_FORMAT_ARGB8888)
#define GRAPHICS_FORMAT_BIT_XRGB8888 (1u << GRAPHICS_FORMAT_XRGB8888)
#define GRAPHICS_FORMAT_BIT_RGB888   (1u << GRAPHICS_FORMAT_RGB888)

// Capability flags
enum GraphicsUapiCapsFlags : uint32_t {
    GRAPHICS_CAP_CREATE_BUFFER = (1u << 0),
    GRAPHICS_CAP_DIRTY_RECT = (1u << 1),
    GRAPHICS_CAP_VSYNC_PRESENT = (1u << 2),
    GRAPHICS_CAP_VBLANK_EVENT = (1u << 3),
    GRAPHICS_CAP_ATOMIC_COMMIT = (1u << 4),
};

// Event types
enum GraphicsUapiEventType : uint32_t {
    GRAPHICS_EVENT_NONE = 0,
    GRAPHICS_EVENT_VBLANK = 1,
    GRAPHICS_EVENT_FLIP_COMPLETE = 2,
};

struct GraphicsUapiCaps {
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t caps_flags;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t preferred_format;
    uint32_t supported_formats_mask;
    uint32_t reserved0;
};

struct GraphicsUapiEvent {
    uint32_t type;
    uint32_t flags;
    uint64_t sequence;
    uint64_t timestamp_ms;
};
