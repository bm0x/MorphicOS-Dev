# Morphic App Build Rules
# Include this file in your app's Makefile to simplify building .mpk packages.
#
# Usage:
#   APP_NAME = myapp
#   APP_SRCS = main.cpp utils.cpp
#   APP_ASSETS = assets/icon.png assets/data.bin
#   include ../../sdk/app.mk
#
# Requirements:
#   - Define MORPHIC_ROOT (or it will be guessed relative to this file)
#   - Define APP_NAME
#   - Define APP_SRCS

ifndef MORPHIC_ROOT
    MORPHIC_ROOT := ../../..
endif

CXX = clang++
LD = ld.lld
ASM = nasm

# Standard flags for Morphic Userspace
CXXFLAGS += -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
            -mcmodel=large -mno-red-zone -nostdlib -I "$(MORPHIC_ROOT)/shared" \
            -I "$(MORPHIC_ROOT)/userspace/sdk" \
            -I "$(MORPHIC_ROOT)/userspace/sdk/gui" -c

ASMFLAGS += -f elf64

# Output files
APP_BIN = $(APP_NAME).bin
APP_MPK = $(APP_NAME).mpk
APP_OBJS = $(APP_SRCS:.cpp=.o)
RUNTIME_OBJ = $(MORPHIC_ROOT)/userspace/sdk/runtime.o
ENTRY_OBJ = $(MORPHIC_ROOT)/userspace/entry.o
SYSCALLS_OBJ = $(MORPHIC_ROOT)/userspace/syscalls.o
GUI_LIB = $(MORPHIC_ROOT)/userspace/sdk/gui/libmorphic_gui.a

.PHONY: all clean

all: $(APP_MPK)

$(RUNTIME_OBJ): $(MORPHIC_ROOT)/userspace/sdk/runtime.cpp
	@echo "  [SDK] Compiling Runtime..."
	$(CXX) $(CXXFLAGS) $< -o $@

# Compile C++ sources
%.o: %.cpp
	@echo "  [APP] Compiling $<..."
	$(CXX) $(CXXFLAGS) $< -o $@

# Link to binary
$(APP_BIN): $(APP_OBJS) $(ENTRY_OBJ) $(SYSCALLS_OBJ) $(RUNTIME_OBJ)
	@echo "  [APP] Linking $(APP_BIN)..."
	$(LD) -T "$(MORPHIC_ROOT)/userspace/linker.ld" -o $@ $(ENTRY_OBJ) $(RUNTIME_OBJ) $(APP_OBJS) $(SYSCALLS_OBJ) "$(GUI_LIB)" --oformat binary

# Pack to MPK
# Check for manifest
MANIFEST_FLAG =
ifneq ("$(wildcard manifest.txt)","")
    MANIFEST_FLAG = --manifest manifest.txt
endif

# Pack to MPK
$(APP_MPK): $(APP_BIN) $(APP_ASSETS)
	@echo "  [APP] Packing $(APP_MPK)..."
	python3 $(MORPHIC_ROOT)/tools/mpk_pack.py $@ $(APP_BIN) $(APP_ASSETS) $(MANIFEST_FLAG) --gen-header mpk_assets.h --prefix $(APP_NAME)

clean:
	rm -f $(APP_OBJS) $(APP_BIN) $(APP_MPK) mpk_assets.h
