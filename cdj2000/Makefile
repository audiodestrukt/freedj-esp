# The CDJ-2000 main board image (SH7763, SH-4A, little-endian).  The compiler
# comes from a Docker image (tools/Dockerfile), so the host needs only Docker:
#   make            build build/main.bin
#   make emu        run it on cdj-nxs2-qemu's emulated CDJ-2000 (console on stdout)
#   make test       boot it on the emulator and check the panel end to end
# Shared with the ESP32 decks, compiled as they are (never edited from here):
#   ../src/deck_event.{h,cpp}            the deck events
#   ../p4/deck/main/prolink.{h,cpp}      Pro DJ Link (on shim/: ESP-IDF headers over our stack)
#   ../p4/deck/main/rekordbox.{h,cpp}    the stick's rekordbox analysis (stdio/dirent on main/files.cpp)
IMG     ?= freedj-sh4-gcc
DOCKER  := docker run --rm -u $(shell id -u):$(shell id -g) -v $(abspath $(CURDIR)/..):/w -w /w/cdj2000 $(IMG)
CROSS   := sh4-linux-gnu-
ARCH    := -m4a -ml -O2 -fno-builtin -nostdlib -fno-pic -fno-stack-protector \
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wall -Wextra -Imain -Ishim -I../src -I../p4/deck/main
CFLAGS  := $(ARCH) -ffreestanding -std=c11
# C++ is compiled hosted (libstdc++'s headers refuse -ffreestanding) but links
# nothing from the system: main/runtime.cpp gives it a heap and its error
# hooks, and instantiates std::string here instead of taking it from libstdc++.
CXXFLAGS := $(ARCH) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
OBJS    := build/start.o build/main.o build/scif.o build/tmu.o build/libc.o build/printf.o build/panel.o \
           build/coop.o build/coop_switch.o build/netstack.o build/usb.o build/fat.o build/files.o build/runtime.o \
           build/deck_event.o build/prolink.o build/rekordbox.o

all: build/main.bin

build/%.o: main/%.c main/*.h | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
build/%.o: main/%.cpp main/*.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
build/%.o: main/%.S | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
build/deck_event.o: ../src/deck_event.cpp ../src/deck_event.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
build/rekordbox.o: ../p4/deck/main/rekordbox.cpp ../p4/deck/main/rekordbox.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
build/prolink.o: ../p4/deck/main/prolink.cpp ../p4/deck/main/prolink.h shim/*.h shim/*/*.h main/*.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -Wno-unused-parameter -c $< -o $@
build/main.elf: $(OBJS) link.ld
	$(DOCKER) $(CROSS)gcc $(ARCH) -T link.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments $(OBJS) -lgcc -o $@
build/main.bin: build/main.elf
	$(DOCKER) $(CROSS)objcopy -O binary $< $@
build:
	mkdir -p build

image:
	docker build -t $(IMG) tools

emu: build/main.bin
	tools/run_emu.sh build/main.bin

test: build/main.bin
	python3 tools/test_panel.py build/main.bin
	python3 tools/test_link.py build/main.bin
	python3 tools/test_usb.py build/main.bin

clean:
	rm -rf build
.PHONY: all image emu test clean
