# Morphic OS

A modern, lightweight operating system built from scratch with a focus on performance and developer experience.

## Features

- **UEFI Boot** - Native UEFI bootloader (no BIOS legacy)
- **MCL Shell** - Morphic Command Language for natural language commands
- **GUI Desktop** - Lightweight desktop environment with modern dark theme
- **60Hz Rendering** - Optimized compositor with dirty rectangles
- **SIMD Optimized** - REP MOVSQ/STOSQ for fast memory operations

## Quick Start

### Prerequisites

```bash
./setup.sh  # Install dependencies (clang, lld, nasm, mtools, qemu, xorriso)
```

### Build & Run

```bash
make         # Build bootloader, kernel, and disk image
./run_web.sh # Run in QEMU with web VNC access
```

### Create Bootable ISO

```bash
make iso     # Creates morphic_os.iso
```

## Project Structure

```
Morphic Project/
├── boot/               # UEFI Bootloader
├── kernel/
│   ├── core/           # Kernel main, shell, panic
│   ├── hal/            # Hardware Abstraction Layer
│   │   ├── video/      # Graphics, compositor, SIMD blit
│   │   ├── input/      # Keyboard, mouse
│   │   └── arch/x86_64/# CPU-specific code
│   ├── mm/             # Memory management, mlock
│   ├── fs/             # VFS, initrd
│   ├── mcl/            # MCL parser and commands
│   ├── gui/            # Desktop widgets
│   └── api/            # Morphic API, render API
├── scripts/            # Build scripts
├── docs/               # Documentation
└── shared/             # Shared headers
```

## MCL Commands

```bash
# Navigation
show path              # Display current directory
open folder:name       # Change directory
go back                # Go to parent directory

# Storage
list files             # List files in current path
list folders           # List directories
create file:name       # Create a file
delete file:name       # Delete a file

# System
show cpu               # CPU information
show memory            # Memory usage
reboot                 # Restart system
```

## Architecture

- **x86_64** target architecture
- **Freestanding** C++ (no stdlib)
- **Custom HAL** for hardware abstraction
- **VFS** for filesystem operations

## License

MIT License - See LICENSE file

## Version

v0.6 - Elite Performance Release
