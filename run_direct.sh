#!/bin/bash
# Morphic OS Direct Runner with Audio and Serial Debug
# For testing with visible QEMU window, audio, and serial console

ISO=${1:-morphic.img}
OVMF=/usr/share/ovmf/OVMF.fd
[ ! -f "$OVMF" ] && OVMF=/usr/share/qemu/OVMF.fd

echo "========================================"
echo "Morphic OS - Debug Mode"
echo "========================================"
echo "Serial output: ENABLED (COM1 -> stdio)"
echo "Type 'desktop' to trigger package loader"
echo "Trace messages will appear in THIS terminal"
echo "Mouse grab: click on the QEMU window (Ctrl+Alt to release)"
echo ""
echo "To save log: ./run_direct.sh 2>&1 | tee boot.log"
echo "========================================"
echo ""

# Direct run with visible window, audio, and serial console (2GB RAM)
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO" \
    -m 2048M \
    -vga std \
    -display gtk,grab-on-hover=on \
    -machine pc,usb=off,i8042=on,pcspk-audiodev=snd0 \
    -serial stdio \
    -audiodev pa,id=snd0 \
    "$@"
