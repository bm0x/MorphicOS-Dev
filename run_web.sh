#!/bin/bash
# Morphic OS Runner with Audio and Enhanced Graphics
# Provides PC Speaker audio and high memory allocation

# Configuration
ISO=morphic_os.iso
OVMF=/usr/share/ovmf/OVMF.fd

# Find OVMF
if [ ! -f "$OVMF" ]; then OVMF=/usr/share/qemu/OVMF.fd; fi
if [ ! -f "$OVMF" ]; then
    echo "Error: OVMF.fd not found. Install 'ovmf' package."
    exit 1
fi

# Parse Arguments
DEBUG=0
if [[ "$1" == "--debug" ]]; then
    DEBUG=1
fi

# Kill previous instances
pkill -f "qemu-system-x86_64" 2>/dev/null
pkill -f "websockify" 2>/dev/null
sleep 1

echo "╔═══════════════════════════════════════════╗"
echo "║       Morphic OS - QEMU Launcher          ║"
echo "╚═══════════════════════════════════════════╝"

# Memory configuration
RAM=2048M          # 2GB RAM (Matches run_direct.sh)

# Create Debug Disk (2GB) if not exists (Copied from run_direct.sh)
if [ ! -f debug_disk.img ]; then
    echo "Creating 2GB Debug Disk..."
    dd if=/dev/zero of=debug_disk.img bs=1M count=2048
    
    # Format as FAT32 if mkfs.vfat exists
    if command -v mkfs.vfat &> /dev/null; then
        echo "Formatting debug_disk.img as FAT32..."
        mkfs.vfat -F 32 debug_disk.img
    else
        echo "mkfs.vfat not found, leaving disk as RAW."
    fi
fi

# Define Flags based on Debug Mode
if [ $DEBUG -eq 1 ]; then
    echo "[!] DEBUG MODE ENABLED: Serial Output to Terminal, Foreground Process"
    QEMU_OPTS="-serial stdio"
    # No daemonize, so we can see output
else
    QEMU_OPTS="-daemonize"
fi

# Start web bridge FIRST (so it's ready)
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
echo "  Debug: $(if [ $DEBUG -eq 1 ]; then echo 'ENABLED (Check terminal)'; else echo 'Disabled'; fi)"
echo ""
echo "  Web:   http://localhost:8080/vnc.html"
echo ""
if [ $DEBUG -eq 1 ]; then
    echo "  [INFO] QEMU is running in foreground. Press Ctrl+C to stop."
    echo "  [INFO] Boot logs follow below:"
    echo "---------------------------------------------------------------"
fi

# Start QEMU with audio support (Logic preserved, but flags injected)
# We use $QEMU_OPTS which contains -daemonize (if normal) or -serial stdio (if debug)

echo "[*] Starting QEMU..."
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO",index=0,media=disk \
    -drive format=raw,file="debug_disk.img",index=1,media=disk \
    -m $RAM \
    -vga std \
    -vnc :0 \
    -audiodev pa,id=speaker \
    -machine pc,usb=off,i8042=on,pcspk-audiodev=speaker \
    $QEMU_OPTS 2>/dev/null || {
        # Fallback without PulseAudio
        echo "[*] Trying SDL audio backend..."
        qemu-system-x86_64 \
            -bios "$OVMF" \
            -drive format=raw,file="$ISO",index=0,media=disk \
            -drive format=raw,file="debug_disk.img",index=1,media=disk \
            -m $RAM \
            -vga std \
            -vnc :0 \
            -audiodev sdl,id=speaker \
            -machine pc,usb=off,i8042=on,pcspk-audiodev=speaker \
            $QEMU_OPTS 2>/dev/null || {
                # Final fallback without audio
                echo "[!] Audio not available, running without sound"
                qemu-system-x86_64 \
                    -bios "$OVMF" \
                    -drive format=raw,file="$ISO",index=0,media=disk \
                    -drive format=raw,file="debug_disk.img",index=1,media=disk \
                    -m $RAM \
                    -vga std \
                    -vnc :0 \
                    $QEMU_OPTS
            }
    }
