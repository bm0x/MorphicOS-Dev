#!/bin/bash
# Morphic OS Professional UEFI ISO Builder
# Creates El Torito UEFI bootable ISO following the official specification

set -e

BUILD_DIR="build"
ISO_ROOT="iso_root"
ISO_NAME="morphic_os.iso"
ESP_IMG="$ISO_ROOT/boot/efi.img"

echo "╔═══════════════════════════════════════════╗"
echo "║   Morphic OS UEFI ISO Builder v2.0        ║"
echo "╚═══════════════════════════════════════════╝"
echo ""

# Verify prerequisites
if [ ! -f "$BUILD_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
    echo "[ERROR] BOOTX64.EFI not found. Run 'make' first."
    exit 1
fi

if [ ! -f "$BUILD_DIR/morph_kernel.elf" ]; then
    echo "[ERROR] Kernel not found. Run 'make' first."
    exit 1
fi

# Clean previous build
rm -rf "$ISO_ROOT" "$ISO_NAME"

# Create directory structure
echo "[1/5] Creating ISO structure..."
mkdir -p "$ISO_ROOT/boot"
mkdir -p "$ISO_ROOT/EFI/BOOT"
mkdir -p "$ISO_ROOT/sys"

# Copy files to ISO root (for ISO9660 access)
cp "$BUILD_DIR/morph_kernel.elf" "$ISO_ROOT/"
cp "$BUILD_DIR/morph_kernel.elf" "$ISO_ROOT/boot/"
cp "$BUILD_DIR/EFI/BOOT/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/"

# Create version info
echo "Morphic OS v0.6" > "$ISO_ROOT/sys/version.txt"
date >> "$ISO_ROOT/sys/version.txt"

# Create EFI System Partition image (FAT12/16)
echo "[2/5] Creating EFI System Partition..."
# Size must be reasonable - using 2880 KB (standard floppy size) or larger
dd if=/dev/zero of="$ESP_IMG" bs=1k count=4096 2>/dev/null

# Format as FAT12
mkfs.fat -F 12 "$ESP_IMG" >/dev/null 2>&1

# Create EFI boot structure inside FAT image
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT

# Copy bootloader and kernel to FAT image
mcopy -i "$ESP_IMG" "$BUILD_DIR/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/
mcopy -i "$ESP_IMG" "$BUILD_DIR/morph_kernel.elf" ::/

echo "[3/5] Verifying FAT image contents..."
mdir -i "$ESP_IMG" ::/EFI/BOOT/

# Create bootable ISO
echo "[4/5] Creating UEFI bootable ISO..."

# Method 1: Using xorriso with proper EFI boot specification
xorriso -as mkisofs \
    -o "$ISO_NAME" \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "MORPHIC_OS" \
    -appid "Morphic OS v0.6" \
    -publisher "Morphic Project" \
    -preparer "make_iso.sh" \
    -eltorito-alt-boot \
    -e boot/efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    "$ISO_ROOT" 2>&1 || {
        echo "[INFO] Trying alternative method..."
        # Fallback without isohybrid
        xorriso -as mkisofs \
            -o "$ISO_NAME" \
            -J -R \
            -V "MORPHIC_OS" \
            -eltorito-alt-boot \
            -e boot/efi.img \
            -no-emul-boot \
            "$ISO_ROOT"
    }

echo "[5/5] Verifying ISO..."
xorriso -indev "$ISO_NAME" -find / -type f 2>&1 | grep -E "\.EFI|\.elf|efi\.img" || true

echo ""
echo "╔═══════════════════════════════════════════╗"
echo "║            ISO BUILD COMPLETE             ║"
echo "╚═══════════════════════════════════════════╝"
ls -lh "$ISO_NAME"
echo ""
echo "Boot methods:"
echo "  QEMU:       qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom $ISO_NAME"
echo "  VirtualBox: Enable EFI in System settings, attach ISO"
echo "  USB:        Hybrid ISO - use dd or Rufus"
