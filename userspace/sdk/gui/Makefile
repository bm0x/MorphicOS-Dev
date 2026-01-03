ifndef MORPHIC_ROOT
    MORPHIC_ROOT := $(abspath ../../../)
endif

CXX = clang++
AR = ar

CXXFLAGS += -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
            -mcmodel=large -mno-red-zone -nostdlib -I "$(MORPHIC_ROOT)/shared" \
            -I "$(MORPHIC_ROOT)/userspace/sdk" -c

LIB_NAME = libmorphic_gui.a
SRCS = compositor.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(LIB_NAME)

$(LIB_NAME): $(OBJS)
	@echo "  [GUI] Creating Library $(LIB_NAME)..."
	$(AR) rcs $@ $^

%.o: %.cpp
	@echo "  [GUI] Compiling $<..."
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(LIB_NAME)
