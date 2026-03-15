#pragma once

#include <stdint.h>
#include "os_event.h"

static constexpr uint32_t COMPOSITOR_PROTOCOL_VERSION = 1;

enum CompositorMessageType : uint32_t {
    COMPOSITOR_MSG_NONE = 0,
    COMPOSITOR_MSG_HELLO = 1,
    COMPOSITOR_MSG_CREATE_SURFACE = 2,
    COMPOSITOR_MSG_COMMIT_SURFACE = 3,
    COMPOSITOR_MSG_SET_FOCUS = 4,
    COMPOSITOR_MSG_INPUT_EVENT = 5,
};

struct CompositorMessage {
    uint32_t type;
    int32_t arg0;
    int32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
};

static inline OSEvent CompositorMessageToEvent(const CompositorMessage& msg) {
    OSEvent ev = {};
    ev.type = OSEvent::USER_MESSAGE;
    ev.dx = msg.arg0;
    ev.dy = msg.arg1;
    ev.buttons = msg.type;
    ev.scancode = msg.arg2;
    ev.ascii = msg.arg3;
    return ev;
}

static inline bool CompositorEventToMessage(const OSEvent& ev, CompositorMessage* out_msg) {
    if (ev.type != OSEvent::USER_MESSAGE || !out_msg) {
        return false;
    }

    out_msg->type = ev.buttons;
    out_msg->arg0 = ev.dx;
    out_msg->arg1 = ev.dy;
    out_msg->arg2 = ev.scancode;
    out_msg->arg3 = ev.ascii;
    return true;
}