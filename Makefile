# Morphic OS Build System

# Tools
CXX = clang++
LD = ld.lld
ASM = nasm

# Flags
CXXFLAGS = -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -mcmodel=large \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387 -I ./shared -c
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
                 kernel/drivers/ramdisk.cpp \
                 kernel/utils/std.cpp \
                 kernel/mm/heap.cpp \
                 kernel/mm/user_heap.cpp \
                 kernel/core/shell.cpp \
                 kernel/process/scheduler.cpp \
                 kernel/fs/vfs.cpp \
                 kernel/fs/initrd.cpp \
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
              kernel/hal/arch/x86_64/syscall_entry.asm

ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)

# GAS assembly (blit_fast.S) handled separately in rules


# Targets

all: image

# --- Bootloader ---
bootloader: build/EFI/BOOT/BOOTX64.EFI

boot/main.o: boot/src/main.cpp
	$(CXX) $(EFI_CXXFLAGS) boot/src/main.cpp -o boot/main.o

build/EFI/BOOT/BOOTX64.EFI: boot/main.o
	mkdir -p build/EFI/BOOT
	lld-link $(EFI_LDFLAGS) /out:build/EFI/BOOT/BOOTX64.EFI boot/main.o

# --- Kernel ---
kernel: build/morph_kernel.elf

# Rule for C++ objects
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

# Rule for ASM objects (.asm)
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Rule for GAS assembly (.S files like blit_fast.S)
kernel/hal/video/blit_fast.o: kernel/hal/video/blit_fast.S
	$(CXX) -target x86_64-elf -c $< -o $@

build/morph_kernel.elf: $(ASM_OBJECTS) $(KERNEL_OBJECTS) kernel/hal/video/blit_fast.o
	mkdir -p build
	$(LD) -T kernel/linker.ld -o build/morph_kernel.elf $(ASM_OBJECTS) $(KERNEL_OBJECTS) kernel/hal/video/blit_fast.o


# --- Image ---
image: bootloader kernel
	dd if=/dev/zero of=morphic.img bs=1M count=64
	mformat -i morphic.img -F ::
	# Copy EFI Structure
	mmd -i morphic.img ::/EFI
	mmd -i morphic.img ::/EFI/BOOT
	mcopy -i morphic.img build/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/
	# Copy Kernel to Root
	mcopy -i morphic.img build/morph_kernel.elf ::/

clean:
	rm -f $(KERNEL_OBJECTS) $(ASM_OBJECTS) boot/main.o build/EFI/BOOT/BOOTX64.EFI build/morph_kernel.elf morphic.img morphic_os.iso

iso:
	bash ./scripts/make_iso.sh