#!/bin/bash

echo "--- Installing Morphic OS Dependencies ---"

# Update apt
sudo apt-get update

# Install build tools
sudo apt-get install -y \
    build-essential \
    nasm \
    mtools \
    dosfstools \
    xorriso \
    qemu-system-x86 \
    clang \
    lld \
    python3-pip \
    git \
    wget

# Install noVNC and Websockify (if not available via apt in older distros, we clone)
# Ubuntu 20.04+ usually has them.
sudo apt-get install -y novnc websockify

# Setup directories
mkdir -p build/EFI/BOOT
mkdir -p kernel/drivers
mkdir -p kernel/hal/arch/x86_64
mkdir -p kernel/hal/video
mkdir -p kernel/mm
mkdir -p kernel/core
mkdir -p boot/src
mkdir -p shared

echo "--- Dependencies Installed. Ready to Build. ---"

# Ensure mkfs.fat is available (used to create EFI System Partition image)
if ! command -v mkfs.fat >/dev/null 2>&1; then
    echo "mkfs.fat not found after install. Attempting to install dosfstools..."
    sudo apt-get update && sudo apt-get install -y dosfstools || \
        echo "Failed to install dosfstools - please install it manually (package: dosfstools)" >&2
fi
