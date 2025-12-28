#!/bin/bash
# Morphic OS Direct Runner with Audio
# For testing with visible QEMU window and audio

ISO=${1:-morphic.img}
OVMF=/usr/share/ovmf/OVMF.fd
[ ! -f "$OVMF" ] && OVMF=/usr/share/qemu/OVMF.fd

echo "Starting Morphic OS with audio..."
echo "Type 'beep' to test PC Speaker"
echo ""

# Direct run with visible window and audio (2GB RAM)
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO" \
    -m 2048M \
    -vga std \
    -audiodev pa,id=snd0 \
    -machine pcspk-audiodev=snd0 \
    "$@"

