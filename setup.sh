#!/bin/bash

echo "--- Installing Morphic OS Dependencies ---"

# Detect OS
# Detect OS
if [ -f /etc/arch-release ]; then
    echo "Detected Arch Linux"
    sudo pacman -Syu --noconfirm
    
    # 1. Install Core Build Tools (High Priority)
    echo "Installing Core Build Tools..."
    sudo pacman -S --noconfirm --needed \
        base-devel \
        nasm \
        mtools \
        dosfstools \
        xorriso \
        qemu-system-x86 \
        clang \
        lld \
        python-pip \
        git \
        wget \
        edk2-ovmf

    # 2. Install Web/VNC Tools (Optional, with fallbacks)
    echo "Installing Web/VNC Tools..."
    # Try installing novnc system package
    if sudo pacman -S --noconfirm --needed novnc >/dev/null 2>&1; then
        echo "novnc installed via pacman."
    else
        echo "novnc package not found. Cloning from git into tools/..."
        mkdir -p tools
        if [ ! -d "tools/noVNC" ]; then
            git clone https://github.com/novnc/noVNC.git tools/noVNC
            git -C tools/noVNC checkout v1.4.0
            ln -sf vnc.html tools/noVNC/index.html
        fi
    fi

    # Try installing websockify
    if sudo pacman -S --noconfirm --needed python-websockify >/dev/null 2>&1; then
         echo "python-websockify installed."
    elif sudo pip install websockify --break-system-packages >/dev/null 2>&1; then
         echo "websockify installed via pip."
    else
         echo "Installing websockify via git in tools/..."
         if [ ! -d "tools/websockify" ]; then
             git clone https://github.com/novnc/websockify.git tools/websockify
         fi
    fi

elif [ -f /etc/debian_version ]; then
    echo "Detected Debian/Ubuntu"
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
else
    echo "Unsupported distribution. Please install dependencies manually."
    exit 1
fi

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
    if [ -f /etc/arch-release ]; then
        sudo pacman -S --noconfirm dosfstools || \
            echo "Failed to install dosfstools - please install it manually (package: dosfstools)" >&2
    elif [ -f /etc/debian_version ]; then
        sudo apt-get update && sudo apt-get install -y dosfstools || \
            echo "Failed to install dosfstools - please install it manually (package: dosfstools)" >&2
    else
        echo "Please install 'dosfstools' manually." >&2
    fi
fi
