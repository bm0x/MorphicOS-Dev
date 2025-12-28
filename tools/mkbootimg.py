#!/usr/bin/env python3
"""
Morphic OS ARM Boot Image Generator
Creates Android-compatible boot.img for ARM devices

Usage: mkbootimg.py --kernel <kernel> --ramdisk <ramdisk> [--dtb <dtb>] -o <output>
"""

import argparse
import struct
import os

# Boot image constants
BOOT_MAGIC = b'ANDROID!'
BOOT_MAGIC_SIZE = 8
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024

# Default load addresses for ARM64
DEFAULT_KERNEL_ADDR = 0x80008000
DEFAULT_RAMDISK_ADDR = 0x81000000
DEFAULT_SECOND_ADDR = 0x80F00000
DEFAULT_TAGS_ADDR = 0x80000100
DEFAULT_PAGE_SIZE = 2048


def align_to_page(size, page_size):
    """Align size to page boundary"""
    return ((size + page_size - 1) // page_size) * page_size


def create_boot_header(kernel_size, ramdisk_size, second_size, dtb_size,
                       kernel_addr, ramdisk_addr, second_addr, tags_addr,
                       page_size, name, cmdline):
    """Create Android boot image header"""
    
    # Header format (boot_img_hdr_v0)
    header = bytearray(page_size)
    
    # Magic
    header[0:8] = BOOT_MAGIC
    
    # Kernel size and address
    struct.pack_into('<I', header, 8, kernel_size)
    struct.pack_into('<I', header, 12, kernel_addr)
    
    # Ramdisk size and address
    struct.pack_into('<I', header, 16, ramdisk_size)
    struct.pack_into('<I', header, 20, ramdisk_addr)
    
    # Second stage size and address
    struct.pack_into('<I', header, 24, second_size)
    struct.pack_into('<I', header, 28, second_addr)
    
    # Tags address (DTB for newer formats)
    struct.pack_into('<I', header, 32, tags_addr)
    
    # Page size
    struct.pack_into('<I', header, 36, page_size)
    
    # Header version (0 for compatibility)
    struct.pack_into('<I', header, 40, 0)
    
    # OS version (optional)
    struct.pack_into('<I', header, 44, 0)
    
    # Name
    name_bytes = name.encode('utf-8')[:BOOT_NAME_SIZE - 1]
    header[48:48 + len(name_bytes)] = name_bytes
    
    # Command line
    cmdline_bytes = cmdline.encode('utf-8')[:BOOT_ARGS_SIZE - 1]
    header[64:64 + len(cmdline_bytes)] = cmdline_bytes
    
    # ID (SHA of kernel + ramdisk, skipped for now)
    # Extra command line (after byte 576)
    
    return bytes(header)


def create_boot_image(kernel_path, ramdisk_path, dtb_path, output_path,
                      kernel_addr=DEFAULT_KERNEL_ADDR,
                      ramdisk_addr=DEFAULT_RAMDISK_ADDR,
                      tags_addr=DEFAULT_TAGS_ADDR,
                      page_size=DEFAULT_PAGE_SIZE,
                      name="MorphicOS",
                      cmdline="console=ttyS0,115200"):
    """Create a complete boot.img"""
    
    # Read kernel
    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()
    kernel_size = len(kernel_data)
    print(f"[+] Kernel: {kernel_path} ({kernel_size} bytes)")
    
    # Read ramdisk
    ramdisk_data = b''
    ramdisk_size = 0
    if ramdisk_path and os.path.exists(ramdisk_path):
        with open(ramdisk_path, 'rb') as f:
            ramdisk_data = f.read()
        ramdisk_size = len(ramdisk_data)
        print(f"[+] Ramdisk: {ramdisk_path} ({ramdisk_size} bytes)")
    
    # Read DTB
    dtb_data = b''
    dtb_size = 0
    if dtb_path and os.path.exists(dtb_path):
        with open(dtb_path, 'rb') as f:
            dtb_data = f.read()
        dtb_size = len(dtb_data)
        print(f"[+] DTB: {dtb_path} ({dtb_size} bytes)")
    
    # Create header
    header = create_boot_header(
        kernel_size, ramdisk_size, 0, dtb_size,
        kernel_addr, ramdisk_addr, DEFAULT_SECOND_ADDR, tags_addr,
        page_size, name, cmdline
    )
    
    # Calculate padded sizes
    kernel_pages = align_to_page(kernel_size, page_size)
    ramdisk_pages = align_to_page(ramdisk_size, page_size) if ramdisk_size else 0
    dtb_pages = align_to_page(dtb_size, page_size) if dtb_size else 0
    
    total_size = page_size + kernel_pages + ramdisk_pages + dtb_pages
    print(f"[+] Total image size: {total_size} bytes")
    
    # Write boot.img
    with open(output_path, 'wb') as f:
        # Header
        f.write(header)
        
        # Kernel (page-aligned)
        f.write(kernel_data)
        f.write(b'\x00' * (kernel_pages - kernel_size))
        
        # Ramdisk (page-aligned)
        if ramdisk_size:
            f.write(ramdisk_data)
            f.write(b'\x00' * (ramdisk_pages - ramdisk_size))
        
        # DTB (page-aligned)
        if dtb_size:
            f.write(dtb_data)
            f.write(b'\x00' * (dtb_pages - dtb_size))
    
    print(f"[+] Created: {output_path}")
    print(f"    Load addresses:")
    print(f"      Kernel:  0x{kernel_addr:08X}")
    print(f"      Ramdisk: 0x{ramdisk_addr:08X}")
    print(f"      Tags:    0x{tags_addr:08X}")
    
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Morphic OS ARM Boot Image Generator'
    )
    parser.add_argument('--kernel', '-k', required=True,
                        help='Path to kernel binary')
    parser.add_argument('--ramdisk', '-r',
                        help='Path to ramdisk/initrd')
    parser.add_argument('--dtb', '-d',
                        help='Path to Device Tree Blob')
    parser.add_argument('--output', '-o', required=True,
                        help='Output boot.img path')
    parser.add_argument('--kernel-addr', type=lambda x: int(x, 0),
                        default=DEFAULT_KERNEL_ADDR,
                        help='Kernel load address (default: 0x80008000)')
    parser.add_argument('--ramdisk-addr', type=lambda x: int(x, 0),
                        default=DEFAULT_RAMDISK_ADDR,
                        help='Ramdisk load address (default: 0x81000000)')
    parser.add_argument('--tags-addr', type=lambda x: int(x, 0),
                        default=DEFAULT_TAGS_ADDR,
                        help='Tags/DTB load address (default: 0x80000100)')
    parser.add_argument('--page-size', type=int,
                        default=DEFAULT_PAGE_SIZE,
                        help='Page size (default: 2048)')
    parser.add_argument('--name', default='MorphicOS',
                        help='Boot image name')
    parser.add_argument('--cmdline', default='console=ttyS0,115200',
                        help='Kernel command line')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.kernel):
        print(f"[-] Error: Kernel not found: {args.kernel}")
        return 1
    
    success = create_boot_image(
        args.kernel,
        args.ramdisk,
        args.dtb,
        args.output,
        kernel_addr=args.kernel_addr,
        ramdisk_addr=args.ramdisk_addr,
        tags_addr=args.tags_addr,
        page_size=args.page_size,
        name=args.name,
        cmdline=args.cmdline
    )
    
    return 0 if success else 1


if __name__ == '__main__':
    exit(main())
