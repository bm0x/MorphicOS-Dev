#!/usr/bin/env python3
"""
Morphic OS Native Cross-Platform Builder & UEFI FAT32 Disk Image Packager.
Allows building Morphic OS natively on Windows (or Linux/macOS) using only:
  - clang++
  - lld-link
  - ld.lld
  - nasm
  - python 3 (no WSL, dd, mtools, or external tar required!)

Usage:
  python tools/build_win.py [release|debug] [clean]
"""

import os
import shutil
import struct
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

KERNEL_CPP_SOURCES = [
    "kernel/core/kernel_main.cpp",
    "kernel/core/bootconfig.cpp",
    "kernel/core/panic.cpp",
    "kernel/core/selftest.cpp",
    "kernel/boot/boot_protocol.cpp",
    "kernel/hal/video/early_term.cpp",
    "kernel/hal/video/font.cpp",
    "kernel/hal/video/font_renderer.cpp",
    "kernel/hal/video/video_manager.cpp",
    "kernel/hal/video/graphics.cpp",
    "kernel/hal/video/vulkan_layer.cpp",
    "kernel/hal/video/verbose.cpp",
    "kernel/hal/video/compositor.cpp",
    "kernel/hal/drm/drm.cpp",
    "kernel/hal/drm/buffer_manager.cpp",
    "kernel/hal/video/vsync.cpp",
    "kernel/hal/video/alpha_lut.cpp",
    "kernel/hal/input/input_manager.cpp",
    "kernel/hal/input/focus_manager.cpp",
    "kernel/hal/input/mouse.cpp",
    "kernel/hal/input/keymap.cpp",
    "kernel/hal/input/evdev.cpp",
    "kernel/hal/audio/audio.cpp",
    "kernel/hal/audio/mixer.cpp",
    "kernel/hal/audio/wav.cpp",
    "kernel/hal/device_registry.cpp",
    "kernel/hal/storage/storage_manager.cpp",
    "kernel/hal/storage/buffer_cache.cpp",
    "kernel/hal/storage/partition.cpp",
    "kernel/mm/pmm.cpp",
    "kernel/mm/write_combining.cpp",
    "kernel/hal/arch/x86_64/gdt.cpp",
    "kernel/hal/arch/x86_64/idt.cpp",
    "kernel/hal/arch/x86_64/tss.cpp",
    "kernel/hal/arch/x86_64/syscall.cpp",
    "kernel/hal/arch/x86_64/interrupt_handlers.cpp",
    "kernel/hal/arch/x86_64/irq_dispatcher.cpp",
    "kernel/hal/arch/x86_64/platform.cpp",
    "kernel/hal/arch/x86_64/tsc.cpp",
    "kernel/hal/arch/x86_64/pci.cpp",
    "kernel/hal/arch/x86_64/mmu.cpp",
    "kernel/drivers/x86/pit.cpp",
    "kernel/drivers/x86/keyboard.cpp",
    "kernel/drivers/x86/uart_8250.cpp",
    "kernel/drivers/x86/ide.cpp",
    "kernel/drivers/ramdisk.cpp",
    "kernel/drivers/gpu/bga.cpp",
    "kernel/fs/drivers/ntfs.cpp",
    "kernel/utils/std.cpp",
    "kernel/mm/heap.cpp",
    "kernel/mm/user_heap.cpp",
    "kernel/core/shell.cpp",
    "kernel/process/scheduler.cpp",
    "kernel/fs/vfs.cpp",
    "kernel/fs/initrd.cpp",
    "kernel/core/loader.cpp",
    "kernel/fs/mount.cpp",
    "kernel/fs/drivers/fat32.cpp",
    "kernel/fs/mount_manager.cpp",
    "kernel/mcl/mcl_parser.cpp",
    "kernel/mcl/mcl_commands.cpp",
    "kernel/mcl/mcl_module.cpp",
    "kernel/ui/bootscreen.cpp",
]

KERNEL_ASM_SOURCES = [
    "kernel/hal/arch/x86_64/tables.asm",
    "kernel/hal/arch/x86_64/interrupts.asm",
    "kernel/hal/arch/x86_64/syscall_entry.asm",
    "kernel/core/entry.asm",
]

USERSPACE_APPS = [
    ("compositor", "userspace/apps/compositor", ["main.cpp"], []),
    ("desktop", "userspace/apps/desktop", ["client_main.cpp"], ["assets/icon.raw", "assets/wallpaper.raw"]),
    ("calculator", "userspace/apps/calculator", ["main.cpp"], []),
    ("terminal", "userspace/apps/terminal", ["main.cpp", "terminal.cpp", "mcl.cpp"], []),
    ("filemanager", "userspace/apps/filemanager", ["main.cpp", "filemanager.cpp"], []),
    ("installer", "userspace/apps/installer", ["main.cpp"], []),
    ("display_probe", "userspace/apps/display_probe", ["main.cpp"], []),
]


def find_tool(name, extra_dirs=()):
    found = shutil.which(name)
    if found:
        return found
    for d in extra_dirs:
        for ext in ("", ".exe"):
            cand = Path(d) / f"{name}{ext}"
            if cand.exists():
                return str(cand)
    return None


def run(cmd, cwd=ROOT):
    res = subprocess.run(cmd, cwd=str(cwd))
    if res.returncode != 0:
        sys.exit(res.returncode)


def format_83_name(filename):
    if filename in (".", ".."):
        return filename.ljust(11, " ").encode("ascii")
    if "." in filename:
        stem, ext = filename.rsplit(".", 1)
    else:
        stem, ext = filename, ""
    stem = stem.upper().replace(" ", "_")[:8].ljust(8, " ")
    ext = ext.upper().replace(" ", "_")[:3].ljust(3, " ")
    return (stem + ext).encode("ascii")


def build_fat32_esp_image(output_path: Path, files_map: dict, size_mb: int = 64):
    """Create a standard superfloppy FAT32 UEFI image in pure Python."""
    bytes_per_sec = 512
    sec_per_clus = 1
    reserved_secs = 32
    num_fats = 2
    total_secs = (size_mb * 1024 * 1024) // bytes_per_sec
    tmp_clusters = total_secs - reserved_secs
    sec_per_fat = ((tmp_clusters * 4) + bytes_per_sec - 1) // bytes_per_sec
    data_start_sec = reserved_secs + (num_fats * sec_per_fat)
    total_clusters = total_secs - data_start_sec

    img = bytearray(total_secs * bytes_per_sec)

    # Boot Sector (BPB)
    img[0:3] = b"\xEB\x58\x90"
    img[3:11] = b"MORPHIC "
    struct.pack_into("<H", img, 11, bytes_per_sec)
    img[13] = sec_per_clus
    struct.pack_into("<H", img, 14, reserved_secs)
    img[16] = num_fats
    struct.pack_into("<H", img, 17, 0)
    struct.pack_into("<H", img, 19, 0)
    img[21] = 0xF8
    struct.pack_into("<H", img, 22, 0)
    struct.pack_into("<H", img, 24, 32)
    struct.pack_into("<H", img, 26, 64)
    struct.pack_into("<I", img, 28, 0)
    struct.pack_into("<I", img, 32, total_secs)
    struct.pack_into("<I", img, 36, sec_per_fat)
    struct.pack_into("<H", img, 40, 0)
    struct.pack_into("<H", img, 42, 0)
    struct.pack_into("<I", img, 44, 2)  # Root cluster = 2
    struct.pack_into("<H", img, 48, 1)  # FSInfo sector
    struct.pack_into("<H", img, 50, 6)  # Backup boot sector
    img[64] = 0x80
    img[66] = 0x29
    struct.pack_into("<I", img, 67, 0x20261123)
    img[71:82] = b"MORPHIC_OS "
    img[82:90] = b"FAT32   "
    struct.pack_into("<H", img, 510, 0xAA55)

    # FSInfo Sector (Sector 1)
    fsinfo_off = bytes_per_sec
    struct.pack_into("<I", img, fsinfo_off + 0, 0x41615252)
    struct.pack_into("<I", img, fsinfo_off + 484, 0x61417272)
    struct.pack_into("<I", img, fsinfo_off + 488, 0xFFFFFFFF)
    struct.pack_into("<I", img, fsinfo_off + 492, 0xFFFFFFFF)
    struct.pack_into("<I", img, fsinfo_off + 508, 0xAA550000)

    # Backup Boot Sector (Sector 6)
    img[6 * bytes_per_sec : 7 * bytes_per_sec] = img[0:bytes_per_sec]

    fat = [0] * (total_clusters + 2)
    fat[0] = 0x0FFFFFF8
    fat[1] = 0x0FFFFFFF
    fat[2] = 0x0FFFFFFF  # Root directory cluster

    next_free_cluster = 3

    def alloc_chain(byte_len):
        nonlocal next_free_cluster
        needed = max(1, (byte_len + bytes_per_sec - 1) // bytes_per_sec)
        chain = []
        for _ in range(needed):
            c = next_free_cluster
            next_free_cluster += 1
            chain.append(c)
        for idx in range(len(chain) - 1):
            fat[chain[idx]] = chain[idx + 1]
        fat[chain[-1]] = 0x0FFFFFFF
        return chain

    def cluster_offset(c):
        return (data_start_sec + (c - 2) * sec_per_clus) * bytes_per_sec

    def lfn_checksum(short11: bytes) -> int:
        s = 0
        for b in short11:
            s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    def make_dir_entries(name_str, attr, first_cluster, size_bytes):
        short11 = format_83_name(name_str)
        short_entry = bytearray(32)
        short_entry[0:11] = short11
        short_entry[11] = attr
        struct.pack_into("<H", short_entry, 20, (first_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", short_entry, 26, first_cluster & 0xFFFF)
        struct.pack_into("<I", short_entry, 28, size_bytes)

        if name_str in (".", ".."):
            return [bytes(short_entry)]

        # Emit VFAT Long Filename (LFN) entries so UEFI finds e.g. "morph_kernel.elf"
        chk = lfn_checksum(short11)
        utf16_chars = [ord(c) for c in name_str] + [0x0000]
        while len(utf16_chars) % 13 != 0:
            utf16_chars.append(0xFFFF)
        num_parts = len(utf16_chars) // 13
        lfn_records = []
        for idx in range(num_parts, 0, -1):
            part = utf16_chars[(idx - 1) * 13 : idx * 13]
            rec = bytearray(32)
            rec[0] = idx | (0x40 if idx == num_parts else 0x00)
            for k in range(5):
                struct.pack_into("<H", rec, 1 + k * 2, part[k])
            rec[11] = 0x0F
            rec[12] = 0x00
            rec[13] = chk
            for k in range(6):
                struct.pack_into("<H", rec, 14 + k * 2, part[5 + k])
            struct.pack_into("<H", rec, 26, 0)
            for k in range(2):
                struct.pack_into("<H", rec, 28 + k * 2, part[11 + k])
            lfn_records.append(bytes(rec))
        return lfn_records + [bytes(short_entry)]

    # Build directory tree
    dirs = {"": []}  # path -> list of 32-byte directory entries
    dir_clusters = {"": [2]}

    for virt_path, host_file in sorted(files_map.items()):
        parts = [p for p in virt_path.strip("/").split("/") if p]
        cur_dir = ""
        for dpart in parts[:-1]:
            parent = cur_dir
            cur_dir = f"{cur_dir}/{dpart}" if cur_dir else dpart
            if cur_dir not in dirs:
                c_chain = alloc_chain(bytes_per_sec)
                dir_clusters[cur_dir] = c_chain
                parent_clus = 0 if parent == "" else dir_clusters[parent][0]
                dirs[cur_dir] = (
                    make_dir_entries(".", 0x10, c_chain[0], 0)
                    + make_dir_entries("..", 0x10, parent_clus, 0)
                )
                dirs[parent].extend(make_dir_entries(dpart, 0x10, c_chain[0], 0))

        data = Path(host_file).read_bytes()
        if len(data) > 0:
            f_chain = alloc_chain(len(data))
            first_c = f_chain[0]
            for idx, c in enumerate(f_chain):
                chunk = data[idx * bytes_per_sec : (idx + 1) * bytes_per_sec]
                off = cluster_offset(c)
                img[off : off + len(chunk)] = chunk
        else:
            first_c = 0
        dirs[cur_dir].extend(make_dir_entries(parts[-1], 0x20, first_c, len(data)))

    # Write directory clusters
    for dpath, entries in dirs.items():
        raw_dir = b"".join(entries)
        needed_clusters = max(1, (len(raw_dir) + bytes_per_sec - 1) // bytes_per_sec)
        chain = dir_clusters[dpath]
        while len(chain) < needed_clusters:
            extra = alloc_chain(bytes_per_sec)[0]
            fat[chain[-1]] = extra
            fat[extra] = 0x0FFFFFFF
            chain.append(extra)
        for idx, c in enumerate(chain):
            chunk = raw_dir[idx * bytes_per_sec : (idx + 1) * bytes_per_sec]
            off = cluster_offset(c)
            img[off : off + len(chunk)] = chunk

    # Write both FAT copies
    for copy_idx in range(num_fats):
        fat_base = (reserved_secs + copy_idx * sec_per_fat) * bytes_per_sec
        for c_idx, val in enumerate(fat):
            pos = fat_base + c_idx * 4
            if pos + 4 <= fat_base + sec_per_fat * bytes_per_sec:
                struct.pack_into("<I", img, pos, val & 0x0FFFFFFF)

    output_path.write_bytes(img)
    print(f"[IMAGE] Built UEFI FAT32 image: {output_path} ({size_mb} MB)")


def build_uefi_iso_from_esp(esp_path: Path, iso_path: Path):
    """Build a UEFI El Torito Bootable ISO-9660 image in pure Python (no xorriso needed)."""
    esp_bytes = esp_path.read_bytes()
    if len(esp_bytes) % 2048 != 0:
        esp_bytes += b"\x00" * (2048 - (len(esp_bytes) % 2048))

    # Layout (2048-byte ISO sectors):
    #   0..15  : System Area (zeros)
    #   16     : Primary Volume Descriptor (PVD)
    #   17     : El Torito Boot Record Volume Descriptor (BRVD)
    #   18     : Volume Descriptor Set Terminator (VDST)
    #   19     : Minimal Root Directory Extent
    #   20     : El Torito Boot Catalog (Platform 0xEF = UEFI)
    #   21..N  : Embedded EFI System Partition (esp_bytes)
    esp_start_lba = 21
    esp_iso_sectors = len(esp_bytes) // 2048
    total_iso_sectors = esp_start_lba + esp_iso_sectors

    iso = bytearray(esp_start_lba * 2048)

    def pack_both16(buf, off, val):
        struct.pack_into("<H", buf, off, val)
        struct.pack_into(">H", buf, off + 2, val)

    def pack_both32(buf, off, val):
        struct.pack_into("<I", buf, off, val)
        struct.pack_into(">I", buf, off + 4, val)

    # Root directory record (34 bytes) pointing to LBA 19, size 2048
    root_rec = bytearray(34)
    root_rec[0] = 34
    root_rec[1] = 0
    pack_both32(root_rec, 2, 19)
    pack_both32(root_rec, 10, 2048)
    root_rec[25] = 0x02  # Directory flag
    pack_both16(root_rec, 28, 1)  # Volume sequence number = 1
    root_rec[32] = 1
    root_rec[33] = 0x00

    # Sector 16: Primary Volume Descriptor
    pvd = bytearray(2048)
    pvd[0] = 0x01
    pvd[1:6] = b"CD001"
    pvd[6] = 0x01
    pvd[8:40] = b"MORPHIC OS".ljust(32, b" ")
    pvd[40:72] = b"MORPHIC_OS".ljust(32, b" ")
    pack_both32(pvd, 80, total_iso_sectors)
    pack_both16(pvd, 120, 1)     # Volume set size
    pack_both16(pvd, 124, 1)     # Volume sequence number
    pack_both16(pvd, 128, 2048)  # Logical block size
    pvd[156:190] = root_rec
    pvd[318:446] = b"MORPHIC PROJECT".ljust(128, b" ")
    pvd[574:702] = b"MORPHIC OS UEFI LIVE".ljust(128, b" ")
    pvd[881] = 0x01
    iso[16 * 2048 : 17 * 2048] = pvd

    # Sector 17: Boot Record Volume Descriptor (El Torito)
    brvd = bytearray(2048)
    brvd[0] = 0x00
    brvd[1:6] = b"CD001"
    brvd[6] = 0x01
    brvd[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", brvd, 71, 20)  # Boot Catalog LBA = 20
    iso[17 * 2048 : 18 * 2048] = brvd

    # Sector 18: Volume Descriptor Set Terminator
    vdst = bytearray(2048)
    vdst[0] = 0xFF
    vdst[1:6] = b"CD001"
    vdst[6] = 0x01
    iso[18 * 2048 : 19 * 2048] = vdst

    # Sector 19: Root Directory Extent (. and ..)
    root_dir = bytearray(2048)
    dot_rec = bytearray(root_rec)
    dot_rec[33] = 0x00
    dotdot_rec = bytearray(root_rec)
    dotdot_rec[33] = 0x01
    root_dir[0:34] = dot_rec
    root_dir[34:68] = dotdot_rec
    iso[19 * 2048 : 20 * 2048] = root_dir

    # Sector 20: El Torito Boot Catalog (Validation Entry + Initial/Default UEFI Entry)
    cat = bytearray(2048)
    # Validation Entry (32 bytes): Header ID = 0x01, Platform ID = 0xEF (EFI)
    cat[0] = 0x01
    cat[1] = 0xEF
    cat[4:28] = b"Morphic OS UEFI".ljust(24, b"\x00")
    cat[30] = 0x55
    cat[31] = 0xAA
    wsum = 0
    for off in range(0, 32, 2):
        if off != 28:
            wsum = (wsum + struct.unpack_from("<H", cat, off)[0]) & 0xFFFF
    struct.pack_into("<H", cat, 28, (-wsum) & 0xFFFF)

    # Default Entry (32 bytes at offset 32): Bootable = 0x88, No Emulation = 0x00
    # Note: El Torito Sector Count is uint16; 0 (or 1) instructs OVMF/VirtualBox/VMware UEFI to read BPB size!
    cat[32] = 0x88
    cat[33] = 0x00
    struct.pack_into("<H", cat, 34, 0)
    struct.pack_into("<H", cat, 38, 1)
    struct.pack_into("<I", cat, 40, esp_start_lba)
    iso[20 * 2048 : 21 * 2048] = cat

    with open(iso_path, "wb") as f:
        f.write(iso)
        f.write(esp_bytes)
    print(f"[ISO] Built UEFI El Torito ISO: {iso_path} ({total_iso_sectors * 2 // 1024} MB)")


def build_fixed_vhd(raw_img_path: Path, vhd_path: Path):
    """Wrap raw disk image with a Microsoft VHD Fixed Disk Footer (Hyper-V Gen 2 & VirtualBox)."""
    raw_bytes = raw_img_path.read_bytes()
    size_bytes = len(raw_bytes)
    total_sectors = size_bytes // 512
    heads = 16
    secs_per_track = 63
    cylinders = min(65535, max(1, total_sectors // (heads * secs_per_track)))

    footer = bytearray(512)
    footer[0:8] = b"conectix"
    struct.pack_into(">I", footer, 8, 0x00000002)       # Features
    struct.pack_into(">I", footer, 12, 0x00010000)      # File format version 1.0
    struct.pack_into(">Q", footer, 16, 0xFFFFFFFFFFFFFFFF)  # Data offset (fixed disk)
    struct.pack_into(">I", footer, 24, 0)               # Timestamp
    footer[28:32] = b"mrph"                             # Creator application
    struct.pack_into(">I", footer, 32, 0x00010000)      # Creator version
    footer[36:40] = b"Wi2k"                             # Creator host OS (Windows)
    struct.pack_into(">Q", footer, 40, size_bytes)      # Original size
    struct.pack_into(">Q", footer, 48, size_bytes)      # Current size
    struct.pack_into(">H", footer, 56, cylinders)
    footer[58] = heads
    footer[59] = secs_per_track
    struct.pack_into(">I", footer, 60, 2)               # Disk type = 2 (Fixed hard disk)
    footer[68:84] = b"\x4D\x4F\x52\x50\x48\x49\x43\x2D\x55\x45\x46\x49\x2D\x56\x48\x44"

    csum = sum(footer)
    struct.pack_into(">I", footer, 64, (~csum) & 0xFFFFFFFF)

    with open(vhd_path, "wb") as f:
        f.write(raw_bytes)
        f.write(footer)
    print(f"[VHD] Built Hyper-V / VirtualBox Fixed VHD: {vhd_path}")


def build_vmware_bundle(raw_img_path: Path, vm_dir: Path):
    """Generate VMware Workstation/Player UEFI .vmx + flat .vmdk descriptor."""
    vm_dir.mkdir(parents=True, exist_ok=True)
    sectors = raw_img_path.stat().st_size // 512
    cyls = max(1, sectors // (16 * 63))
    abs_raw = str(raw_img_path.resolve())

    vmdk_text = (
        '# Disk DescriptorFile\n'
        'version=1\n'
        'CID=fffffffe\n'
        'parentCID=ffffffff\n'
        'createType="monolithicFlat"\n\n'
        '# Extent description\n'
        f'RW {sectors} FLAT "{abs_raw}" 0\n\n'
        '# The Disk Data Base\n'
        'ddb.virtualHWVersion = "14"\n'
        f'ddb.geometry.cylinders = "{cyls}"\n'
        'ddb.geometry.heads = "16"\n'
        'ddb.geometry.sectors = "63"\n'
        'ddb.adapterType = "ide"\n'
    )
    (vm_dir / "MorphicOS.vmdk").write_text(vmdk_text, encoding="utf-8")

    vmx_text = (
        '.encoding = "UTF-8"\n'
        'config.version = "8"\n'
        'virtualHW.version = "16"\n'
        'displayName = "Morphic OS (UEFI)"\n'
        'guestOS = "other-64"\n'
        'firmware = "efi"\n'
        'memsize = "2048"\n'
        'numvcpus = "2"\n'
        'svga.vramSize = "134217728"\n'
        'ide0:0.present = "TRUE"\n'
        'ide0:0.fileName = "MorphicOS.vmdk"\n'
        'serial0.present = "TRUE"\n'
        'serial0.fileType = "file"\n'
        'serial0.fileName = "boot_vmware.log"\n'
    )
    (vm_dir / "MorphicOS.vmx").write_text(vmx_text, encoding="utf-8")
    print(f"[VMWARE] Built VMware UEFI bundle: {vm_dir / 'MorphicOS.vmx'}")


def main():
    mode = "release"
    for arg in sys.argv[1:]:
        if arg == "clean":
            for p in ROOT.rglob("*.o"):
                p.unlink(missing_ok=True)
            for p in ROOT.glob("userspace/*.mpk"):
                p.unlink(missing_ok=True)
            (ROOT / "userspace/initrd.img").unlink(missing_ok=True)
            (ROOT / "morphic.img").unlink(missing_ok=True)
            print("[CLEAN] Completed.")
            return
        elif arg in ("release", "debug"):
            mode = arg

    llvm_dirs = [r"C:\Program Files\LLVM\bin", r"C:\Program Files (x86)\LLVM\bin"]
    nasm_dirs = [
        r"C:\Program Files\NASM",
        r"C:\NASM",
        os.path.expandvars(r"%LOCALAPPDATA%\bin\NASM"),
        os.path.expandvars(r"%LOCALAPPDATA%\Programs\NASM"),
    ]

    cxx = find_tool("clang++", llvm_dirs)
    lld_link = find_tool("lld-link", llvm_dirs)
    ld_lld = find_tool("ld.lld", llvm_dirs)
    nasm = find_tool("nasm", nasm_dirs)

    missing = [n for n, v in [("clang++", cxx), ("lld-link", lld_link), ("ld.lld", ld_lld), ("nasm", nasm)] if not v]
    if missing:
        print(f"[ERROR] Missing native toolchain binaries: {', '.join(missing)}")
        print("Install on Windows with:")
        print("  winget install -e --id LLVM.LLVM && winget install -e --id NASM.NASM && winget install -e --id SoftwareFreedomConservancy.QEMU")
        sys.exit(1)

    # 1. Bootloader
    (ROOT / "build/EFI/BOOT").mkdir(parents=True, exist_ok=True)
    run([cxx, "-target", "x86_64-pc-win32-coff", "-fno-stack-protector", "-fshort-wchar",
         "-mno-red-zone", "-nostdlibinc", "-I", "./boot/src", "-I", "./shared", "-c",
         "boot/src/main.cpp", "-o", "boot/main.o"])
    run([lld_link, "/subsystem:efi_application", "/entry:EfiMain",
         "/out:build/EFI/BOOT/BOOTX64.EFI", "boot/main.o"])

    # 2. Kernel
    base_flags = ["-target", "x86_64-elf", "-ffreestanding", "-fno-rtti", "-fno-exceptions",
                  "-mno-red-zone", "-mcmodel=large", "-I", "./shared", "-c", "-DEMBED_DESKTOP"]
    opt_flags = ["-O2", "-march=x86-64", "-msse2"] if mode == "release" else ["-O0", "-g", "-DDEBUG"]

    objs = []
    for asm_src in KERNEL_ASM_SOURCES:
        obj = asm_src[:-4] + ".o"
        run([nasm, "-f", "elf64", asm_src, "-o", obj])
        objs.append(obj)

    for cpp_src in KERNEL_CPP_SOURCES:
        obj = cpp_src[:-4] + ".o"
        run([cxx] + base_flags + opt_flags + [cpp_src, "-o", obj])
        objs.append(obj)

    blit_obj = "kernel/hal/video/blit_fast.o"
    blit_flags = ["-target", "x86_64-elf", "-O2", "-msse2", "-c"] if mode == "release" else ["-target", "x86_64-elf", "-O0", "-g", "-c"]
    run([cxx] + blit_flags + ["kernel/hal/video/blit_fast.S", "-o", blit_obj])
    objs.append(blit_obj)

    run([ld_lld, "-T", "kernel/linker.ld", "-o", "build/morph_kernel.elf"] + objs)

    # 3. Userspace SDK + Apps
    run([nasm, "-f", "elf64", "userspace/entry.asm", "-o", "userspace/entry.o"])
    run([nasm, "-f", "elf64", "userspace/syscalls.asm", "-o", "userspace/syscalls.o"])

    u_flags = ["-target", "x86_64-elf", "-ffreestanding", "-fno-rtti", "-fno-exceptions",
               "-Wno-new-returns-null", "-Wno-nonnull",
               "-mcmodel=large", "-mno-red-zone", "-nostdlib", "-O2",
               "-I", "shared", "-I", "userspace/sdk", "-I", "userspace/sdk/gui", "-c"]

    run([cxx] + u_flags + ["userspace/sdk/runtime.cpp", "-o", "userspace/sdk/runtime.o"])
    run([cxx] + u_flags + ["userspace/sdk/gui/compositor.cpp", "-o", "userspace/sdk/gui/compositor.o"])

    mpk_files = []
    for app_name, app_dir_str, srcs, assets in USERSPACE_APPS:
        app_dir = ROOT / app_dir_str
        # Pre-generate mpk_assets.h if assets or manifest exist
        dummy_bin = app_dir / f"{app_name}.tmp.bin"
        dummy_bin.write_bytes(b"\x00")
        pack_cmd = [sys.executable, str(ROOT / "tools/mpk_pack.py"), str(app_dir / f"{app_name}.mpk"), str(dummy_bin)]
        pack_cmd += [str(app_dir / a) for a in assets]
        if (app_dir / "manifest.txt").exists():
            pack_cmd += ["--manifest", str(app_dir / "manifest.txt")]
        pack_cmd += ["--gen-header", str(app_dir / "mpk_assets.h"), "--prefix", app_name]
        run(pack_cmd, cwd=app_dir)
        dummy_bin.unlink(missing_ok=True)

        app_objs = []
        for s in srcs:
            s_path = app_dir / s
            o_path = app_dir / (Path(s).stem + ".o")
            run([cxx] + u_flags + ["-I", str(app_dir), str(s_path), "-o", str(o_path)])
            app_objs.append(str(o_path))

        app_bin = app_dir / f"{app_name}.bin"
        run([ld_lld, "-T", "userspace/linker.ld", "-z", "max-page-size=4096", "-o", str(app_bin),
             "userspace/entry.o", "userspace/sdk/runtime.o"] + app_objs +
            ["userspace/syscalls.o", "userspace/sdk/gui/compositor.o", "--oformat", "binary"])

        out_mpk = ROOT / "userspace" / f"{app_name}.mpk"
        pack_cmd = [sys.executable, str(ROOT / "tools/mpk_pack.py"), str(out_mpk), str(app_bin)]
        pack_cmd += [str(app_dir / a) for a in assets]
        if (app_dir / "manifest.txt").exists():
            pack_cmd += ["--manifest", str(app_dir / "manifest.txt")]
        run(pack_cmd, cwd=app_dir)
        mpk_files.append(out_mpk)

    # 4. Pack InitRD TAR in pure Python (POSIX ustar format)
    initrd_path = ROOT / "userspace/initrd.img"
    with tarfile.open(initrd_path, "w", format=tarfile.USTAR_FORMAT) as tf:
        for mpk in mpk_files:
            tf.add(mpk, arcname=mpk.name)
    print(f"[INITRD] Packed {len(mpk_files)} apps -> {initrd_path}")

    # 5. Build bootable UEFI FAT32 image + ISO + Hyper-V VHD + VMware VMX
    raw_img = ROOT / "morphic.img"
    files_map = {
        "EFI/BOOT/BOOTX64.EFI": ROOT / "build/EFI/BOOT/BOOTX64.EFI",
        "morph_kernel.elf": ROOT / "build/morph_kernel.elf",
        "initrd.img": initrd_path,
    }
    build_fat32_esp_image(raw_img, files_map, size_mb=64)
    build_uefi_iso_from_esp(raw_img, ROOT / "morphic_os.iso")
    build_fixed_vhd(raw_img, ROOT / "morphic_os.vhd")
    build_vmware_bundle(raw_img, ROOT / "build/vmware")


if __name__ == "__main__":
    main()
