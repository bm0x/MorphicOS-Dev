#include "../../sdk/morphic_syscalls.h"
#include "../../sdk/graphics_uapi.h"
#include "../../shared/graphics_uapi_shared.h"

#include <stdint.h>

static void DebugPrint(const char* s) {
    sys_debug_print(s);
}

static void PrintU64(uint64_t value) {
    char buf[32];
    int pos = 0;

    if (value == 0) {
        buf[pos++] = '0';
    } else {
        char rev[32];
        int rpos = 0;
        while (value > 0 && rpos < (int)sizeof(rev)) {
            rev[rpos++] = (char)('0' + (value % 10));
            value /= 10;
        }
        while (rpos > 0) {
            buf[pos++] = rev[--rpos];
        }
    }

    buf[pos++] = '\n';
    buf[pos] = 0;
    DebugPrint(buf);
}

static void PrintHexU64(uint64_t value) {
    static const char* kHex = "0123456789ABCDEF";
    char buf[32];
    int pos = 0;
    buf[pos++] = '0';
    buf[pos++] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[pos++] = kHex[(value >> (i * 4)) & 0xF];
    }
    buf[pos++] = '\n';
    buf[pos] = 0;
    DebugPrint(buf);
}

extern "C" int main(void* args) {
    (void)args;

    DebugPrint("[display_probe] start\n");

    GraphicsUapiCaps caps = {};
    if (!MorphicGfx::QueryCaps(&caps)) {
        DebugPrint("[display_probe] QueryCaps failed\n");
        return 1;
    }

    DebugPrint("[display_probe] UAPI version major:\n");
    PrintU64(caps.version_major);

    DebugPrint("[display_probe] UAPI version minor:\n");
    PrintU64(caps.version_minor);

    DebugPrint("[display_probe] caps flags:\n");
    PrintHexU64(caps.caps_flags);

    DebugPrint("[display_probe] max_width:\n");
    PrintU64(caps.max_width);

    DebugPrint("[display_probe] max_height:\n");
    PrintU64(caps.max_height);

    DebugPrint("[display_probe] preferred_format:\n");
    PrintU64(caps.preferred_format);

    DebugPrint("[display_probe] supported_formats_mask:\n");
    PrintHexU64(caps.supported_formats_mask);

    if ((caps.caps_flags & GRAPHICS_CAP_ATOMIC_COMMIT) != 0) {
        DebugPrint("[display_probe] atomic: test full\n");
        int ok_test_full = MorphicGfx::AtomicTestFull(true);
        PrintU64((uint64_t)ok_test_full);

        DebugPrint("[display_probe] atomic: commit full\n");
        int ok_commit_full = MorphicGfx::AtomicCommitFull(true);
        PrintU64((uint64_t)ok_commit_full);

        DebugPrint("[display_probe] atomic: test rect\n");
        int ok_test_rect = MorphicGfx::AtomicTest(0, 0, 64, 64, MorphicGfx::ATOMIC_WAIT_VSYNC);
        PrintU64((uint64_t)ok_test_rect);

        DebugPrint("[display_probe] atomic: commit rect\n");
        int ok_commit_rect = MorphicGfx::AtomicCommit(0, 0, 64, 64, MorphicGfx::ATOMIC_WAIT_VSYNC);
        PrintU64((uint64_t)ok_commit_rect);
    } else {
        DebugPrint("[display_probe] atomic: not supported\n");
    }

    // Generate a few present calls to produce vblank events in the v1 stub.
    for (int i = 0; i < 3; i++) {
        (void)MorphicGfx::Present(1); // bit0 = vsync
        sys_sleep(5);
    }

    DebugPrint("[display_probe] polling events...\n");
    int received = 0;
    int vblank_events = 0;
    int flip_events = 0;
    for (int i = 0; i < 100; i++) {
        GraphicsUapiEvent ev = {};
        if (MorphicGfx::PollEvent(&ev)) {
            DebugPrint("[display_probe] event type:\n");
            PrintU64(ev.type);
            DebugPrint("[display_probe] event seq:\n");
            PrintU64(ev.sequence);
            if (ev.type == GRAPHICS_EVENT_VBLANK) {
                vblank_events++;
            } else if (ev.type == GRAPHICS_EVENT_FLIP_COMPLETE) {
                flip_events++;
            }
            received++;
        } else {
            sys_sleep(10);
        }
    }

    DebugPrint("[display_probe] total events:\n");
    PrintU64((uint64_t)received);
    DebugPrint("[display_probe] vblank events:\n");
    PrintU64((uint64_t)vblank_events);
    DebugPrint("[display_probe] flip events:\n");
    PrintU64((uint64_t)flip_events);
    DebugPrint("[display_probe] done\n");
    return 0;
}
