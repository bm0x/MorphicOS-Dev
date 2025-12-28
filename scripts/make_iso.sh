#!/bin/bash
# Morphic OS ISO Builder
# Creates bootable ISO using El Torito specification for UEFI
# Requires: xorriso

set -e

ISO_DIR="iso_root"
ISO_NAME="morphic_os.iso"
BUILD_DIR="build"

echo "=== Morphic OS ISO Builder ==="
echo ""

# Check for required tools
if ! command -v xorriso &> /dev/null; then
    echo "[ERROR] xorriso not found. Install with: sudo apt install xorriso"
    exit 1
fi

# Clean and create ISO structure
echo "[1/5] Creating ISO structure..."
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/EFI/BOOT"
mkdir -p "$ISO_DIR/boot"
mkdir -p "$ISO_DIR/sys"

# Copy bootloader
echo "[2/5] Copying bootloader..."
if [ ! -f "$BUILD_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
    echo "[ERROR] Bootloader not found. Run 'make bootloader' first."
    exit 1
fi
cp "$BUILD_DIR/EFI/BOOT/BOOTX64.EFI" "$ISO_DIR/EFI/BOOT/"

# Copy kernel
echo "[3/5] Copying kernel..."
if [ ! -f "$BUILD_DIR/morph_kernel.elf" ]; then
    echo "[ERROR] Kernel not found. Run 'make kernel' first."
    exit 1
fi
cp "$BUILD_DIR/morph_kernel.elf" "$ISO_DIR/boot/"

# Create system info file
echo "[4/5] Creating system files..."
cat > "$ISO_DIR/sys/version.txt" << EOF
Morphic OS v0.6 - Elite Performance
Build Date: $(date +%Y-%m-%d)
Architecture: x86_64 UEFI
EOF

# Create ISO with El Torito (UEFI boot)
echo "[5/5] Creating ISO..."
xorriso -as mkisofs \
    -R -J \
    -V "MORPHIC_OS" \
    -o "$ISO_NAME" \
    -e EFI/BOOT/BOOTX64.EFI \
    -no-emul-boot \
    "$ISO_DIR"

# Show result
echo ""
echo "=== ISO Creation Complete ==="
ls -lh "$ISO_NAME"
echo ""
echo "Usage:"
echo "  - VirtualBox: Settings > Storage > Add Optical Drive"
echo "  - QEMU: qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom $ISO_NAME"
echo "  - USB: sudo dd if=$ISO_NAME of=/dev/sdX bs=4M status=progress"
echo ""
echo "Done!"
