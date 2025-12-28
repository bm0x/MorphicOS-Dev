#!/bin/bash

# Configuration
ISO=morphic.img
OVMF=/usr/share/ovmf/OVMF.fd # Adjust path if needed, usually here on Ubuntu
# If OVMF not found there, try typical locations
if [ ! -f "$OVMF" ]; then
    OVMF=/usr/share/qemu/OVMF.fd
fi
if [ ! -f "$OVMF" ]; then
    echo "Error: OVMF.fd not found. Please install 'ovmf' package."
    exit 1
fi

# Kill previous instances
pkill -f "qemu-system-x86_64"
pkill -f "websockify"

echo "--- Starting Morphic OS in QEMU (Headless) ---"
# Start QEMU with VNC on :0 (5900)
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO" \
    -m 512M \
    -vga std \
    -vnc :0 \
    -daemonize

echo "--- Starting web bridge on :8080 ---"
# Find location of noVNC
NOVNC_DIR=/usr/share/novnc
if [ ! -d "$NOVNC_DIR" ]; then
    echo "Warning: noVNC directory not found at standard location. Assuming websockify can serve generic."
    # We will just run websockify blindly
    websockify -D --web=/usr/share/novnc 8080 localhost:5900
else
    websockify -D --web="$NOVNC_DIR" 8080 localhost:5900
fi

echo "--- Done! ---"
echo "Open standard: http://localhost:8080/vnc.html?host=localhost&port=8080"
echo "Use 'Connect' button in UI."
