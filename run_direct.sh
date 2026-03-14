#!/bin/bash
# Morphic OS Direct Runner with improved graphics options
# - Enables KVM if available, uses host CPU, enables GL for the GTK display

set -e

ISO=${1:-morphic_os.iso}
OS_NAME="$(uname -s)"
HOST_ARCH="$(uname -m)"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "Error: qemu-system-x86_64 not found. Run ./setup.sh first."
    exit 1
fi

find_ovmf() {
    local candidates=(
        "./OVMF.fd"
        "/usr/share/ovmf/OVMF.fd"
        "/usr/share/qemu/OVMF.fd"
        "/usr/share/edk2/x64/OVMF.fd"
        "/usr/share/edk2-ovmf/x64/OVMF.fd"
        "/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
        "/usr/local/share/qemu/edk2-x86_64-code.fd"
    )

    local path
    for path in "${candidates[@]}"; do
        if [ -f "$path" ]; then
            echo "$path"
            return 0
        fi
    done

    return 1
}

OVMF="$(find_ovmf || true)"
if [ -z "$OVMF" ]; then
    echo "Error: OVMF firmware not found (expected OVMF.fd or edk2-x86_64-code.fd)."
    echo "Hint: keep OVMF.fd in project root or install via package manager."
    exit 1
fi

if [ "$OS_NAME" = "Darwin" ] && [ "$HOST_ARCH" = "arm64" ]; then
    CPUS=${CPUS:-2}
else
    CPUS=${CPUS:-4}
fi
RAM=${RAM:-2048M}
GL=${GL:-1}
FULLSCREEN=${FULLSCREEN:-0}

if [ ! -f "$ISO" ]; then
    echo "Error: disk image not found: $ISO"
    echo "Build first with: make or make iso"
    exit 1
fi

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
    MKFS_TOOL=""
    if command -v mkfs.vfat >/dev/null 2>&1; then
        MKFS_TOOL="$(command -v mkfs.vfat)"
    elif command -v mkfs.fat >/dev/null 2>&1; then
        MKFS_TOOL="$(command -v mkfs.fat)"
    elif [ -x "/opt/homebrew/opt/dosfstools/sbin/mkfs.fat" ]; then
        MKFS_TOOL="/opt/homebrew/opt/dosfstools/sbin/mkfs.fat"
    elif [ -x "/usr/local/opt/dosfstools/sbin/mkfs.fat" ]; then
        MKFS_TOOL="/usr/local/opt/dosfstools/sbin/mkfs.fat"
    fi

    # Format as FAT32 if mkfs tool exists
    if [ -n "$MKFS_TOOL" ]; then
        echo "Formatting debug_disk.img as FAT32..."
        "$MKFS_TOOL" -F 32 debug_disk.img >/dev/null 2>&1 || true
    else
        echo "mkfs.vfat/mkfs.fat not found, leaving disk as RAW."
    fi
fi

# KVM and CPU options
ACCEL_OPTS=""
CPU_OPTS=""
if [ "$OS_NAME" = "Linux" ] && [ -c /dev/kvm ]; then
    ACCEL_OPTS="-accel kvm"
    CPU_OPTS="-cpu host"
elif [ "$OS_NAME" = "Darwin" ] && [ "${ACCEL:-tcg}" = "hvf" ] && qemu-system-x86_64 -accel help 2>/dev/null | grep -qw hvf; then
    ACCEL_OPTS="-accel hvf"
    CPU_OPTS="-cpu host"
else
    ACCEL_OPTS="-accel tcg,thread=multi"
    CPU_OPTS="-cpu qemu64"
fi

# Detect display backend
DISPLAY_HELP="$(qemu-system-x86_64 -display help 2>&1 || true)"
DISPLAY_BACKEND="${DISPLAY_BACKEND:-}"
if [ -z "$DISPLAY_BACKEND" ]; then
    if [ "$OS_NAME" = "Darwin" ] && echo "$DISPLAY_HELP" | grep -qw cocoa; then
        DISPLAY_BACKEND="cocoa"
    elif echo "$DISPLAY_HELP" | grep -qw gtk; then
        DISPLAY_BACKEND="gtk"
    elif echo "$DISPLAY_HELP" | grep -qw sdl; then
        DISPLAY_BACKEND="sdl"
    fi
fi

if [ -z "$DISPLAY_BACKEND" ]; then
    echo "Error: no supported QEMU display backend found (tried cocoa/gtk/sdl)."
    exit 1
fi

DISPLAY_OPTS=""
case "$DISPLAY_BACKEND" in
    cocoa)
        DISPLAY_OPTS="-display cocoa,show-cursor=on"
        ;;
    gtk)
        if [ "$GL" = "1" ]; then
            DISPLAY_OPTS="-display gtk,gl=on,grab-on-hover=on"
        else
            DISPLAY_OPTS="-display gtk,grab-on-hover=on"
        fi
        ;;
    sdl)
        if [ "$GL" = "1" ]; then
            DISPLAY_OPTS="-display sdl,gl=on"
        else
            DISPLAY_OPTS="-display sdl"
        fi
        ;;
    *)
        echo "Error: unsupported DISPLAY_BACKEND=$DISPLAY_BACKEND"
        exit 1
        ;;
esac

# Audio backend selection
AUDIO_HELP="$(qemu-system-x86_64 -audiodev help 2>/dev/null || true)"
AUDIO_BACKEND="${AUDIO_BACKEND:-auto}"
if [ "$AUDIO_BACKEND" = "auto" ]; then
    if [ "$OS_NAME" = "Darwin" ]; then
        for candidate in coreaudio sdl; do
            if echo "$AUDIO_HELP" | grep -qw "$candidate"; then
                AUDIO_BACKEND="$candidate"
                break
            fi
        done
    else
        for candidate in pa pipewire alsa sdl; do
            if echo "$AUDIO_HELP" | grep -qw "$candidate"; then
                AUDIO_BACKEND="$candidate"
                break
            fi
        done
    fi
fi

MACHINE_OPTS="-machine pc,i8042=on"
AUDIO_OPTS=""
if [ -n "$AUDIO_BACKEND" ] && [ "$AUDIO_BACKEND" != "none" ] && echo "$AUDIO_HELP" | grep -qw "$AUDIO_BACKEND"; then
    MACHINE_OPTS="-machine pc,i8042=on,pcspk-audiodev=snd0"
    AUDIO_OPTS="-audiodev ${AUDIO_BACKEND},id=snd0"
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
    $MACHINE_OPTS \
    -serial stdio \
    $AUDIO_OPTS \
    $ACCEL_OPTS \
    $CPU_OPTS \
    -smp $CPUS \
    $( [ "$FULLSCREEN" = "1" ] && echo -full-screen ) \
    ${@:2}
