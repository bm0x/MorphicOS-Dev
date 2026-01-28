#!/bin/bash
# Morphic OS Direct Runner with improved graphics options
# - Enables KVM if available, uses host CPU, enables GL for the GTK display

ISO=${1:-morphic_os.iso}
OVMF=/usr/share/ovmf/OVMF.fd
[ ! -f "$OVMF" ] && OVMF=/usr/share/qemu/OVMF.fd

CPUS=${CPUS:-4}
RAM=${RAM:-2048M}
GL=${GL:-1}
FULLSCREEN=${FULLSCREEN:-0}

echo "========================================"
echo "Morphic OS - Direct Runner (graphics tuned)"
echo "========================================"
echo "ISO: $ISO"
echo "Serial output: ENABLED (COM1 -> stdio)"
echo "CPUs: $CPUS  RAM: $RAM  GL accel: $GL"
echo "To save log: ./run_direct.sh 2>&1 | tee boot.log"
echo "Mouse grab: click on QEMU window (Ctrl+Alt to release)"
echo "========================================"

# Create Debug Disk (2GB) if not exists
if [ ! -f debug_disk.img ]; then
    echo "Creating 2GB Debug Disk..."
    dd if=/dev/zero of=debug_disk.img bs=1M count=2048 status=none
    # Format as FAT32 if mkfs.vfat exists
    if command -v mkfs.vfat &> /dev/null; then
        echo "Formatting debug_disk.img as FAT32..."
        mkfs.vfat -F 32 debug_disk.img >/dev/null 2>&1 || true
    else
        echo "mkfs.vfat not found, leaving disk as RAW."
    fi
fi

# KVM and CPU options
KVM_OPTS=""
if [ -c /dev/kvm ]; then
    KVM_OPTS="-enable-kvm -cpu host -smp $CPUS"
else
    KVM_OPTS="-cpu qemu64 -smp $CPUS"
fi

# Display options (enable GL if requested)
if [ "$GL" = "1" ]; then
    DISPLAY_OPTS="-display gtk,gl=on,grab-on-hover=on"
else
    DISPLAY_OPTS="-display gtk,grab-on-hover=on"
fi

# Use standard VGA (good compatibility with BGA driver), but enable GL on the GTK frontend
VGA_OPTS="-vga std"

# Build and run QEMU command (expanded safely)
echo "Launching QEMU..."
cd "$(dirname "$0")"

exec qemu-system-x86_64 \
    -bios "$OVMF" \
    -drive format=raw,file="$ISO",index=0,media=disk \
    -drive format=raw,file="debug_disk.img",index=1,media=disk \
    -m $RAM \
    $VGA_OPTS \
    $DISPLAY_OPTS \
    -machine pc,i8042=on,pcspk-audiodev=snd0 \
    -serial stdio \
    -audiodev pa,id=snd0 \
    $KVM_OPTS \
    $( [ "$FULLSCREEN" = "1" ] && echo -full-screen ) \
    ${@:2}
