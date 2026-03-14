#!/bin/bash

set -e

echo "--- Installing Morphic OS Dependencies ---"

OS_NAME="$(uname -s)"

install_arch() {
    echo "Detected Arch Linux"
    sudo pacman -Syu --noconfirm

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

    echo "Installing Web/VNC Tools..."
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
}

install_debian() {
    echo "Detected Debian/Ubuntu"
    sudo apt-get update

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

    sudo apt-get install -y novnc websockify
}

install_macos() {
    echo "Detected macOS"

    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew is required on macOS. Install from https://brew.sh and run this script again."
        exit 1
    fi

    brew update

    echo "Installing Core Build Tools with Homebrew..."
    brew install llvm nasm mtools dosfstools xorriso qemu python3 git wget

    echo "Installing Web/VNC Tools..."
    if brew install novnc >/dev/null 2>&1; then
        echo "novnc installed via brew."
    else
        echo "novnc formula unavailable. Cloning into tools/noVNC..."
        mkdir -p tools
        if [ ! -d "tools/noVNC" ]; then
            git clone https://github.com/novnc/noVNC.git tools/noVNC
            git -C tools/noVNC checkout v1.4.0
            ln -sf vnc.html tools/noVNC/index.html
        fi
    fi

    if brew install websockify >/dev/null 2>&1; then
        echo "websockify installed via brew."
    elif pip3 install --user websockify >/dev/null 2>&1; then
        echo "websockify installed via pip (user scope)."
    else
        echo "Installing websockify via git in tools/..."
        if [ ! -d "tools/websockify" ]; then
            git clone https://github.com/novnc/websockify.git tools/websockify
        fi
    fi

    BREW_PREFIX="$(brew --prefix)"
    if [ -d "$BREW_PREFIX/opt/llvm/bin" ]; then
        export PATH="$BREW_PREFIX/opt/llvm/bin:$PATH"
    fi
}

if [ "$OS_NAME" = "Darwin" ]; then
    install_macos
elif [ -f /etc/arch-release ]; then
    install_arch
elif [ -f /etc/debian_version ]; then
    install_debian
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
    if [ "$OS_NAME" = "Darwin" ]; then
        brew install dosfstools || \
            echo "Failed to install dosfstools - please install it manually (brew install dosfstools)" >&2
    elif [ -f /etc/arch-release ]; then
        sudo pacman -S --noconfirm dosfstools || \
            echo "Failed to install dosfstools - please install it manually (package: dosfstools)" >&2
    elif [ -f /etc/debian_version ]; then
        sudo apt-get update && sudo apt-get install -y dosfstools || \
            echo "Failed to install dosfstools - please install it manually (package: dosfstools)" >&2
    else
        echo "Please install 'dosfstools' manually." >&2
    fi
fi
