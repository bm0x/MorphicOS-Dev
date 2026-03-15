#pragma once

#include "../../shared/compositor_protocol.h"
#include "morphic_syscalls.h"

namespace MorphicCompositor {

inline uint64_t CurrentPid() {
    return sys_get_pid();
}

inline uint64_t ServerPid() {
    return sys_get_compositor_pid();
}

inline void PostMessage(uint64_t target_pid, const CompositorMessage& msg) {
    OSEvent ev = CompositorMessageToEvent(msg);
    sys_post_message(target_pid, &ev);
}

inline bool PostHello(uint64_t target_pid, uint32_t client_pid, uint32_t width, uint32_t height) {
    if (target_pid == 0 || client_pid == 0) {
        return false;
    }

    CompositorMessage msg = {};
    msg.type = COMPOSITOR_MSG_HELLO;
    msg.arg0 = (int32_t)client_pid;
    msg.arg1 = (int32_t)COMPOSITOR_PROTOCOL_VERSION;
    msg.arg2 = width;
    msg.arg3 = height;
    PostMessage(target_pid, msg);
    return true;
}

inline bool PostCreateSurface(uint64_t target_pid, uint32_t client_pid, uint32_t width, uint32_t height) {
    if (target_pid == 0 || client_pid == 0) {
        return false;
    }

    CompositorMessage msg = {};
    msg.type = COMPOSITOR_MSG_CREATE_SURFACE;
    msg.arg0 = (int32_t)client_pid;
    msg.arg1 = 0;
    msg.arg2 = width;
    msg.arg3 = height;
    PostMessage(target_pid, msg);
    return true;
}

inline bool PostCommitSurface(uint64_t target_pid, uint32_t client_pid, uint32_t width, uint32_t height) {
    if (target_pid == 0 || client_pid == 0) {
        return false;
    }

    CompositorMessage msg = {};
    msg.type = COMPOSITOR_MSG_COMMIT_SURFACE;
    msg.arg0 = (int32_t)client_pid;
    msg.arg1 = 0;
    msg.arg2 = width;
    msg.arg3 = height;
    PostMessage(target_pid, msg);
    return true;
}

inline bool PostCommitSurfaceWithSerial(uint64_t target_pid,
                                        uint32_t client_pid,
                                        uint32_t frame_serial,
                                        uint32_t width,
                                        uint32_t height) {
    if (target_pid == 0 || client_pid == 0) {
        return false;
    }

    CompositorMessage msg = {};
    msg.type = COMPOSITOR_MSG_COMMIT_SURFACE;
    msg.arg0 = (int32_t)client_pid;
    msg.arg1 = (int32_t)frame_serial;
    msg.arg2 = width;
    msg.arg3 = height;
    PostMessage(target_pid, msg);
    return true;
}

inline bool PostFrameDone(uint64_t target_pid,
                          uint32_t client_pid,
                          uint32_t frame_serial,
                          uint32_t flags) {
    if (target_pid == 0 || client_pid == 0) {
        return false;
    }

    CompositorMessage msg = {};
    msg.type = COMPOSITOR_MSG_FRAME_DONE;
    msg.arg0 = (int32_t)client_pid;
    msg.arg1 = (int32_t)frame_serial;
    msg.arg2 = 0;
    msg.arg3 = flags;
    PostMessage(target_pid, msg);
    return true;
}

inline bool DecodeMessage(const OSEvent& ev, CompositorMessage* out_msg) {
    return CompositorEventToMessage(ev, out_msg);
}

}