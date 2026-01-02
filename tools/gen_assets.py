#!/usr/bin/env python3
import os

def create_raw_image(filename, width, height, color_func):
    with open(filename, 'wb') as f:
        for y in range(height):
            for x in range(width):
                # BGRA format (Little Endian uint32: B G R A)
                b, g, r, a = color_func(x, y, width, height)
                f.write(bytes([b, g, r, a]))

def wallpaper_pattern(x, y, w, h):
    # Simple gradient
    r = int((x / w) * 255)
    g = int((y / h) * 255)
    b = 100
    a = 255
    return (b, g, r, a)

def icon_pattern(x, y, w, h):
    # Red circle
    cx, cy = w/2, h/2
    dx, dy = x - cx, y - cy
    if dx*dx + dy*dy < (w/2 - 2)**2:
        return (0, 0, 255, 255) # Red (B=0, G=0, R=255)
    return (0, 0, 0, 0) # Transparent

if __name__ == "__main__":
    print("Generating assets...")
    # Wallpaper: 1024x768 (as assumed in desktop.cpp)
    create_raw_image("userspace/wallpaper.raw", 1024, 768, wallpaper_pattern)
    # Icon: 64x64
    create_raw_image("userspace/icon.raw", 64, 64, icon_pattern)
    print("Assets generated.")
