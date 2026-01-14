# Morphic OS Build System

# Tools
CXX = clang++
LD = ld.lld
ASM = nasm

# Flags
CXXFLAGS = -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mcmodel=large \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387 -I ./shared -c \
           -DEMBED_DESKTOP

# Optional debug toggles
MOUSE_DEBUG ?= 0
ifeq ($(MOUSE_DEBUG),1)
    CXXFLAGS += -DMOUSE_DEBUG
endif
ASMFLAGS = -f elf64
EFI_CXXFLAGS = -target x86_64-pc-win32-coff -fno-stack-protector -fshort-wchar -mno-red-zone -nostdlibinc -I ./boot/src -I ./shared -c
EFI_LDFLAGS = /subsystem:efi_application /entry:EfiMain

# Files
KERNEL_SOURCES = kernel/core/kernel_main.cpp \
                 kernel/core/bootconfig.cpp \
                 kernel/core/panic.cpp \
                 kernel/boot/boot_protocol.cpp \
                 kernel/hal/video/early_term.cpp \
                 kernel/hal/video/font.cpp \
                 kernel/hal/video/font_renderer.cpp \
                 kernel/hal/video/video_manager.cpp \
                 kernel/hal/video/graphics.cpp \
                 kernel/hal/video/verbose.cpp \
                 kernel/hal/video/compositor.cpp \
                 kernel/hal/video/vsync.cpp \
                 kernel/hal/input/input_manager.cpp \
                 kernel/hal/input/mouse.cpp \
                 kernel/hal/input/keymap.cpp \
                 kernel/hal/audio/audio.cpp \
                 kernel/hal/audio/mixer.cpp \
                 kernel/hal/audio/wav.cpp \
                 kernel/hal/device_registry.cpp \
                 kernel/hal/storage/storage_manager.cpp \
                 kernel/hal/storage/buffer_cache.cpp \
                 kernel/hal/storage/partition.cpp \
                 kernel/mm/pmm.cpp \
                 kernel/mm/write_combining.cpp \
                 kernel/hal/arch/x86_64/gdt.cpp \
                 kernel/hal/arch/x86_64/idt.cpp \
                 kernel/hal/arch/x86_64/tss.cpp \
                 kernel/hal/arch/x86_64/syscall.cpp \
                 kernel/hal/arch/x86_64/interrupt_handlers.cpp \
                 kernel/hal/arch/x86_64/irq_dispatcher.cpp \
                 kernel/hal/arch/x86_64/platform.cpp \
                 kernel/hal/arch/x86_64/mmu.cpp \
                 kernel/drivers/x86/pit.cpp \
                 kernel/drivers/x86/keyboard.cpp \
                 kernel/drivers/x86/uart_8250.cpp \
                 kernel/drivers/x86/ide.cpp \
                 kernel/drivers/ramdisk.cpp \
                 kernel/utils/std.cpp \
                 kernel/mm/heap.cpp \
                 kernel/mm/user_heap.cpp \
                 kernel/core/shell.cpp \
                 kernel/process/scheduler.cpp \
                 kernel/fs/vfs.cpp \
                 kernel/fs/initrd.cpp \
                 kernel/core/loader.cpp \
                 kernel/fs/mount.cpp \
                 kernel/mcl/mcl_parser.cpp \
                 kernel/mcl/mcl_commands.cpp \
                 kernel/mcl/mcl_module.cpp \
                 kernel/api/morphic_api.cpp \
                 kernel/gui/widgets.cpp \
                 kernel/gui/desktop.cpp

KERNEL_OBJECTS = $(KERNEL_SOURCES:.cpp=.o)


# NASM assembly sources
ASM_SOURCES = kernel/hal/arch/x86_64/tables.asm \
              kernel/hal/arch/x86_64/interrupts.asm \
              kernel/hal/arch/x86_64/syscall_entry.asm \
              kernel/core/entry.asm

ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)

# GAS assembly (blit_fast.S) handled separately in rules


# Targets

all: image

# --- Bootloader ---
bootloader: build/EFI/BOOT/BOOTX64.EFI

boot/main.o: boot/src/main.cpp
	@echo "  [BOOT] Compiling Main..."
	$(CXX) $(EFI_CXXFLAGS) boot/src/main.cpp -o boot/main.o

build/EFI/BOOT/BOOTX64.EFI: boot/main.o
	@echo "========================================"
	@echo "  [BOOT] Linking Bootloader..."
	@echo "========================================"
	mkdir -p build/EFI/BOOT
	lld-link $(EFI_LDFLAGS) /out:build/EFI/BOOT/BOOTX64.EFI boot/main.o

# --- Kernel ---
kernel: build/morph_kernel.elf

# Rule for C++ objects
%.o: %.cpp
	@echo "  [KERNEL] Compiling $<..."
	$(CXX) $(CXXFLAGS) $< -o $@

# Rule for ASM objects (.asm)
%.o: %.asm
	@echo "  [KERNEL] Assembling $<..."
	$(ASM) $(ASMFLAGS) $< -o $@

# Rule for GAS assembly (.S files like blit_fast.S)
kernel/hal/video/blit_fast.o: kernel/hal/video/blit_fast.S
	@echo "  [KERNEL] Assembling $<..."
	$(CXX) -target x86_64-elf -c $< -o $@

build/morph_kernel.elf: $(ASM_OBJECTS) $(KERNEL_OBJECTS) kernel/hal/video/blit_fast.o
	@echo "========================================"
	@echo "  [KERNEL] Linking Kernel..."
	@echo "========================================"
	mkdir -p build
	$(LD) -T kernel/linker.ld -o build/morph_kernel.elf $(ASM_OBJECTS) $(KERNEL_OBJECTS) kernel/hal/video/blit_fast.o




# --- Userspace ---
DESKTOP_APP_DIR = userspace/apps/desktop
userspace/syscalls.o: userspace/syscalls.asm
	@echo "  [USER] Assembling Syscalls..."
	$(ASM) $(ASMFLAGS) $< -o $@

userspace/entry.o: userspace/entry.asm
	@echo "  [USER] Assembling Entry..."
	$(ASM) $(ASMFLAGS) $< -o $@

# Delegate desktop build to its own Makefile (SDK Standard)
userspace/desktop.mpk: userspace/syscalls.o userspace/entry.o
	@echo "========================================"
	@echo "  [USER] Building Desktop App..."
	@echo "========================================"
	$(MAKE) -C $(DESKTOP_APP_DIR)
	cp $(DESKTOP_APP_DIR)/desktop.mpk userspace/desktop.mpk

CALCULATOR_APP_DIR = userspace/apps/calculator
CALCULATOR_APP_DIR = userspace/apps/calculator
userspace/calculator.mpk: userspace/syscalls.o userspace/entry.o
	@echo "========================================"
	@echo "  [USER] Building Calculator App..."
	@echo "========================================"
	$(MAKE) -C $(CALCULATOR_APP_DIR)
	cp $(CALCULATOR_APP_DIR)/calculator.mpk userspace/calculator.mpk

TERMINAL_APP_DIR = userspace/apps/terminal
TERMINAL_APP_DIR = userspace/apps/terminal
userspace/terminal.mpk: userspace/syscalls.o userspace/entry.o
	@echo "========================================"
	@echo "  [USER] Building Terminal App..."
	@echo "========================================"
	$(MAKE) -C $(TERMINAL_APP_DIR)
	cp $(TERMINAL_APP_DIR)/terminal.mpk userspace/terminal.mpk


kernel/fs/desktop_mpk.cpp: userspace/desktop.mpk
	@echo "  [KERNEL] Embedding Desktop MPK..."
	python3 tools/bin2h.py userspace/desktop.mpk kernel/fs/desktop_mpk.cpp desktop_mpk

kernel/fs/desktop_mpk.o: kernel/fs/desktop_mpk.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

kernel/fs/calculator_mpk.cpp: userspace/calculator.mpk
	@echo "  [KERNEL] Embedding Calculator MPK..."
	python3 tools/bin2h.py userspace/calculator.mpk kernel/fs/calculator_mpk.cpp calculator_mpk

kernel/fs/calculator_mpk.o: kernel/fs/calculator_mpk.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

kernel/fs/terminal_mpk.cpp: userspace/terminal.mpk
	@echo "  [KERNEL] Embedding Terminal MPK..."
	python3 tools/bin2h.py userspace/terminal.mpk kernel/fs/terminal_mpk.cpp terminal_mpk

kernel/fs/terminal_mpk.o: kernel/fs/terminal_mpk.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@


# --- Image ---
image: bootloader kernel userspace/desktop.mpk userspace/calculator.mpk userspace/terminal.mpk
	@echo "========================================"
	@echo "  [IMAGE] Creating Disk Image..."
	@echo "========================================"
	dd if=/dev/zero of=morphic.img bs=1M count=64
	mformat -i morphic.img -F ::
	# Copy EFI Structure
	mmd -i morphic.img ::/EFI
	mmd -i morphic.img ::/EFI/BOOT
	mcopy -i morphic.img build/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/
	# Copy Kernel to Root
	mcopy -i morphic.img build/morph_kernel.elf ::/
	# Copy Desktop Package to Root
	mcopy -i morphic.img userspace/desktop.mpk ::/
	mcopy -i morphic.img userspace/calculator.mpk ::/
	mcopy -i morphic.img userspace/terminal.mpk ::/


clean:
	rm -f $(KERNEL_OBJECTS) $(ASM_OBJECTS) boot/main.o build/EFI/BOOT/BOOTX64.EFI build/morph_kernel.elf morphic.img morphic_os.iso kernel/fs/*_mpk.cpp kernel/fs/*_mpk.o userspace/*.mpk userspace/initrd.img
	# Dynamic Cleanup of Apps
	rm -f userspace/apps/*/*.mpk userspace/apps/*/*.bin userspace/apps/*/*.o
	$(MAKE) -C $(DESKTOP_APP_DIR) clean

# --- InitRD ---
# Create a standard TAR image containing all userspace applications
initrd: userspace/desktop.mpk userspace/calculator.mpk userspace/terminal.mpk
	@echo "========================================"
	@echo "  [INITRD] Packing initrd.img..."
	@echo "========================================"
	cd userspace && tar -cvf initrd.img desktop.mpk calculator.mpk terminal.mpk

# --- ISO ---
ISO_ROOT = iso_root
ISO_NAME = morphic_os.iso
ESP_IMG = $(ISO_ROOT)/boot/efi.img

iso: kernel bootloader initrd
	@echo "========================================"
	@echo "  [ISO] Creating UEFI Bootable ISO..."
	@echo "========================================"
	rm -rf $(ISO_ROOT) $(ISO_NAME)
	
	# Create Structure
	mkdir -p $(ISO_ROOT)/boot
	mkdir -p $(ISO_ROOT)/EFI/BOOT
	mkdir -p $(ISO_ROOT)/sys
	
	# Copy Core Files
	cp build/morph_kernel.elf $(ISO_ROOT)/
	cp build/morph_kernel.elf $(ISO_ROOT)/boot/
	cp build/EFI/BOOT/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	
	# Copy InitRD
	cp userspace/initrd.img $(ISO_ROOT)/
	
	# Version Info
	echo "Morphic OS v0.6" > $(ISO_ROOT)/sys/version.txt
	date >> $(ISO_ROOT)/sys/version.txt
	
	# Create EFI System Partition (ESP) - 64MB to fit all apps + kernel
	@echo "  [ISO] Creating ESP Image..."
	dd if=/dev/zero of=$(ESP_IMG) bs=1M count=64 2>/dev/null
	mkfs.fat -F 16 $(ESP_IMG) >/dev/null 2>&1
	mmd -i $(ESP_IMG) ::/EFI
	mmd -i $(ESP_IMG) ::/EFI/BOOT
	mcopy -i $(ESP_IMG) build/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/
	mcopy -i $(ESP_IMG) build/morph_kernel.elf ::/
	# Copy InitRD to ESP
	mcopy -i $(ESP_IMG) userspace/initrd.img ::/
	
	# Build ISO
	@echo "  [ISO] Running Xorriso..."
	xorriso -as mkisofs \
		-o $(ISO_NAME) \
		-iso-level 3 \
		-full-iso9660-filenames \
		-volid "MORPHIC_OS" \
		-appid "Morphic OS v0.6" \
		-publisher "Morphic Project" \
		-preparer "Makefile" \
		-eltorito-alt-boot \
		-e boot/efi.img \
		-no-emul-boot \
		-isohybrid-gpt-basdat \
		$(ISO_ROOT) 2>/dev/null
	
	@echo "========================================"
	@echo "  [ISO] Build Complete: $(ISO_NAME)"
	@echo "========================================"