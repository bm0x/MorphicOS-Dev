APP_NAME = calculator
SRCS = main.cpp ../../../kernel/utils/std.cpp
OBJS = main.o std.o ../../entry.o ../../syscalls.o

CXX = g++
LD = ld
CXXFLAGS = -I../../sdk -I../../../shared -I../../../kernel -fno-rtti -fno-exceptions -nostdlib -fno-builtin -fno-use-cxa-atexit -mcmodel=large
LDFLAGS = -T ../../linker.ld -z max-page-size=0x1000

all: $(APP_NAME).mpk

$(APP_NAME).mpk: $(OBJS)
	$(LD) $(LDFLAGS) -o $(APP_NAME).elf $(OBJS)
	# Convert ELF to MPK (using objcopy or custom tool)
	# For now, we just rename ELF to MPK as our loader handles ELF
	cp $(APP_NAME).elf $(APP_NAME).mpk

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

std.o: ../../../kernel/utils/std.cpp
	$(CXX) $(CXXFLAGS) -c ../../../kernel/utils/std.cpp -o std.o

clean:
	rm -f *.o $(APP_NAME).elf $(APP_NAME).mpk
