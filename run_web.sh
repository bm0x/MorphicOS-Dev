#!/bin/bash
# Morphic OS Runner with Audio and Enhanced Graphics
# Provides PC Speaker audio and high memory allocation

set -e

# Configuration
ISO=morphic_os.iso
OS_NAME="$(uname -s)"
HOST_ARCH="$(uname -m)"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "Error: qemu-system-x86_64 not found. Run ./setup.sh first."
    exit 1
fi

if [ ! -f "$ISO" ]; then
    echo "Error: disk image not found: $ISO"
    echo "Build first with: make or make iso"
    exit 1
fi

# Find OVMF
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
    exit 1
fi

# Parse Arguments
DEBUG=0
if [[ "$1" == "--debug" ]]; then
    DEBUG=1
fi

# Kill previous instances
pkill -f "qemu-system-x86_64" 2>/dev/null || true
pkill -f "websockify" 2>/dev/null || true
sleep 1

echo "╔═══════════════════════════════════════════╗"
echo "║       Morphic OS - QEMU Launcher          ║"
echo "╚═══════════════════════════════════════════╝"

# Memory configuration
RAM=2048M          # 2GB RAM (Matches run_direct.sh)
if [ "$OS_NAME" = "Darwin" ] && [ "$HOST_ARCH" = "arm64" ]; then
    CPUS=${CPUS:-2}
else
    CPUS=${CPUS:-4}
fi

# Create Debug Disk (2GB) if not exists (Copied from run_direct.sh)
if [ ! -f debug_disk.img ]; then
    echo "Creating 2GB Debug Disk..."
    dd if=/dev/zero of=debug_disk.img bs=1M count=2048
    
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
        "$MKFS_TOOL" -F 32 debug_disk.img
    else
        echo "mkfs.vfat/mkfs.fat not found, leaving disk as RAW."
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

# Find noVNC
NOVNC_DIR=""
if [ -d "/usr/share/novnc" ]; then
    NOVNC_DIR="/usr/share/novnc"
elif [ -d "/opt/homebrew/share/novnc" ]; then
    NOVNC_DIR="/opt/homebrew/share/novnc"
elif [ -d "/usr/local/share/novnc" ]; then
    NOVNC_DIR="/usr/local/share/novnc"
elif [ -d "tools/noVNC" ]; then
    NOVNC_DIR="tools/noVNC"
else
    echo "Error: noVNC not found. Please run setup.sh"
    exit 1
fi

# Find websockify
WEBSOCKIFY_CMD="websockify"
if [ -f "tools/websockify/run" ]; then
    WEBSOCKIFY_CMD="./tools/websockify/run"
elif ! command -v websockify >/dev/null; then
    echo "Error: websockify not found. Please run setup.sh"
    exit 1
fi

$WEBSOCKIFY_CMD -D --web="$NOVNC_DIR" 8080 localhost:5900 2>/dev/null

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

# Acceleration / CPU options
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

AUDIO_HELP="$(qemu-system-x86_64 -audiodev help 2>/dev/null || true)"

audio_supported() {
    local driver="$1"
    if [ "$driver" = "none" ]; then
        return 0
    fi
    echo "$AUDIO_HELP" | grep -qw "$driver"
}

run_qemu_with_audio() {
    local backend="$1"
    local machine_opts="-machine pc,usb=off,i8042=on"
    local audio_opts=()

    if [ "$backend" != "none" ]; then
        machine_opts="-machine pc,usb=off,i8042=on,pcspk-audiodev=speaker"
        audio_opts=( -audiodev "$backend,id=speaker" )
    fi

    qemu-system-x86_64 \
        -bios "$OVMF" \
        -drive format=raw,file="$ISO",index=0,media=disk \
        -drive format=raw,file="debug_disk.img",index=1,media=disk \
        -m $RAM \
        -smp $CPUS \
        -vga std \
        -vnc :0 \
        $machine_opts \
        "${audio_opts[@]}" \
        $ACCEL_OPTS \
        $CPU_OPTS \
        $QEMU_OPTS
}

if [ "$OS_NAME" = "Darwin" ]; then
    AUDIO_CANDIDATES=(coreaudio sdl none)
else
    AUDIO_CANDIDATES=(pa pipewire alsa sdl none)
fi

# Start QEMU with audio support (Logic preserved, but flags injected)
# We use $QEMU_OPTS which contains -daemonize (if normal) or -serial stdio (if debug)

echo "[*] Starting QEMU..."
LAUNCHED=0
for backend in "${AUDIO_CANDIDATES[@]}"; do
    if ! audio_supported "$backend"; then
        continue
    fi

    if [ "$backend" = "none" ]; then
        echo "[*] Starting without audio backend..."
    else
        echo "[*] Trying audio backend: $backend"
    fi

    if run_qemu_with_audio "$backend"; then
        LAUNCHED=1
        break
    fi
done

if [ $LAUNCHED -ne 1 ]; then
    echo "Error: failed to start qemu-system-x86_64 with available audio backends."
    exit 1
fi
