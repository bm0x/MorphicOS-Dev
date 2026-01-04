#!/bin/bash
# Morphic OS Direct Runner with Audio and Serial Debug
# For testing with visible QEMU window, audio, and serial console

ISO=${1:-morphic_os.iso}
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

# Create Debug Disk (2GB) if not exists
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

# Direct run with visible window, audio, and serial console (2GB RAM)
qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO",index=0,media=disk \
    -drive format=raw,file="debug_disk.img",index=1,media=disk \
    -m 2048M \
    -vga std \
    -display gtk,grab-on-hover=on \
    -machine pc,i8042=on,pcspk-audiodev=snd0 \
    -serial stdio \
    -audiodev pa,id=snd0 \
    "$@"
