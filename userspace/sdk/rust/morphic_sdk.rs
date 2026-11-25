//! Morphic OS Ring-3 Rust SDK (`#![no_std]`)
//! Memory-safe bindings to Morphic OS syscalls, Glass Dark framebuffer, and MorphicHW.
//! Build target: `x86_64-unknown-none` linked with `userspace/entry.o` + `userspace/syscalls.o`.

#![no_std]

use core::ffi::c_void;

#[repr(C)]
#[derive(Copy, Clone, Default)]
pub struct MorphicNetInfo {
    pub present: u32,
    pub link_up: u32,
    pub vendor_id: u16,
    pub device_id: u16,
    pub mac: [u8; 6],
    pub reserved: u16,
    pub tx_packets: u64,
    pub rx_packets: u64,
    pub driver_name: [u8; 24],
}

extern "C" {
    pub fn sys_get_screen_info() -> u64;
    pub fn sys_get_time_ms() -> u64;
    pub fn sys_get_pid() -> u64;
    pub fn sys_sleep(ms: u32);
    pub fn sys_create_window(w: u32, h: u32, flags: u32) -> *mut u32;
    pub fn sys_video_flip(backbuffer: *mut c_void) -> u64;
    pub fn sys_debug_print(msg: *const u8);
    pub fn sys_spawn(path: *const u8) -> i32;
    pub fn sys_drm_set_display_mode(refresh_hz: u32, vsync_mode: u32) -> i32;
    pub fn sys_net_info(out_net_info: *mut MorphicNetInfo) -> i32;
}

pub mod theme {
    pub const BG_CANVAS: u32 = 0xFF0B101B;
    pub const SURFACE_CARD: u32 = 0xFF172033;
    pub const ACCENT_CYAN: u32 = 0xFF38BDF8;
    pub const SUCCESS: u32 = 0xFF34D399;
    pub const TEXT_PRIMARY: u32 = 0xFFF1F5F9;
}

pub struct Surface {
    pub ptr: *mut u32,
    pub width: usize,
    pub height: usize,
}

impl Surface {
    pub fn create(width: u32, height: u32) -> Option<Self> {
        let ptr = unsafe { sys_create_window(width, height, 0) };
        if ptr.is_null() {
            None
        } else {
            Some(Self { ptr, width: width as usize, height: height as usize })
        }
    }

    pub fn clear(&mut self, argb: u32) {
        let total = self.width * self.height;
        let slice = unsafe { core::slice::from_raw_parts_mut(self.ptr, total) };
        for px in slice.iter_mut() {
            *px = argb;
        }
    }

    pub fn fill_rect(&mut self, x: usize, y: usize, w: usize, h: usize, argb: u32) {
        for row in y..(y + h).min(self.height) {
            let base = row * self.width;
            for col in x..(x + w).min(self.width) {
                unsafe { *self.ptr.add(base + col) = argb; }
            }
        }
    }

    pub fn present(&mut self) {
        unsafe { sys_video_flip(self.ptr as *mut c_void); }
    }
}

pub mod hw {
    use super::*;

    pub fn set_refresh_rate(hz: u32) -> bool {
        unsafe { sys_drm_set_display_mode(hz, 0) != 0 }
    }

    pub fn net_info() -> Option<MorphicNetInfo> {
        let mut info = MorphicNetInfo::default();
        let ok = unsafe { sys_net_info(&mut info as *mut MorphicNetInfo) != 0 };
        if ok { Some(info) } else { None }
    }

    pub fn sleep_ms(ms: u32) {
        unsafe { sys_sleep(ms); }
    }
}
