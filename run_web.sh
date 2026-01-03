#!/bin/bash
# Morphic OS Runner with Audio and Enhanced Graphics
# Provides PC Speaker audio and high memory allocation

# Configuration
ISO=morphic.img
OVMF=/usr/share/ovmf/OVMF.fd

# Find OVMF
if [ ! -f "$OVMF" ]; then OVMF=/usr/share/qemu/OVMF.fd; fi
if [ ! -f "$OVMF" ]; then
    echo "Error: OVMF.fd not found. Install 'ovmf' package."
    exit 1
fi

# Kill previous instances
pkill -f "qemu-system-x86_64" 2>/dev/null
pkill -f "websockify" 2>/dev/null
sleep 1

echo "╔═══════════════════════════════════════════╗"
echo "║       Morphic OS - QEMU Launcher          ║"
echo "╚═══════════════════════════════════════════╝"

# Memory configuration
RAM=1024M          # 1GB RAM (plenty for video + kernel)

# Start QEMU with audio support
echo "[*] Starting QEMU with PC Speaker audio..."
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO" \
    -m $RAM \
    -vga std \
    -vnc :0 \
    -audiodev pa,id=speaker \
    -machine pc,usb=off,i8042=on,pcspk-audiodev=speaker \
    -daemonize 2>/dev/null || {
        # Fallback without PulseAudio
        echo "[*] Trying SDL audio backend..."
        qemu-system-x86_64 \
            -bios "$OVMF" \
            -drive format=raw,file="$ISO" \
            -m $RAM \
            -vga std \
            -vnc :0 \
            -audiodev sdl,id=speaker \
            -machine pc,usb=off,i8042=on,pcspk-audiodev=speaker \
            -daemonize 2>/dev/null || {
                # Final fallback without audio
                echo "[!] Audio not available, running without sound"
                qemu-system-x86_64 \
                    -bios "$OVMF" \
                    -drive format=raw,file="$ISO" \
                    -m $RAM \
                    -vga std \
                    -vnc :0 \
                    -daemonize
            }
    }

# Start web bridge
echo "[*] Starting web VNC bridge..."
NOVNC_DIR=/usr/share/novnc
websockify -D --web="$NOVNC_DIR" 8080 localhost:5900 2>/dev/null

echo ""
echo "╔═══════════════════════════════════════════╗"
echo "║              Ready!                       ║"
echo "╚═══════════════════════════════════════════╝"
echo ""
echo "  RAM:   $RAM"
echo "  Audio: PC Speaker (beep command)"
echo ""
echo "  Web:   http://localhost:8080/vnc.html"
echo ""
echo "  Test beep: type 'beep' in Morphic shell"
