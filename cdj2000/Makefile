# The CDJ-2000 main board image (SH7763, SH-4A, little-endian).  The compiler
# comes from a Docker image (tools/Dockerfile), so the host needs only Docker:
#   make            build build/main.bin
#   make emu        run it on cdj-nxs2-qemu's emulated CDJ-2000 (console on stdout)
#   make test       boot it on the emulator and check the panel end to end
# Shared with the ESP32 decks, compiled as they are (never edited from here):
#   ../src/deck_event.{h,cpp}            the deck events
#   ../p4/deck/main/prolink.{h,cpp}      Pro DJ Link (on shim/: ESP-IDF headers over our stack)
#   ../p4/deck/main/rekordbox.{h,cpp}    the stick's rekordbox analysis (stdio/dirent on main/files.cpp)
#   ../p4/deck/main/stream.{h,cpp}       the track stream: WAV, MP3 (minimp3) into a window round the playhead
#   ../src/wsola.{h,cpp}                 time stretching for MASTER TEMPO
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
           build/dsp.o build/dsp_blob.o build/engine.o \
           build/stream.o build/wsola.o build/mp3.o \
           build/deck_event.o build/prolink.o build/rekordbox.o

all: build/main.bin build/gui.flash

build/%.o: main/%.c main/*.h | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
build/%.o: main/%.cpp main/*.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
# glibc's headers mark FILE* and DIR* arguments nonnull; ours check anyway.
build/files.o: CXXFLAGS += -Wno-nonnull-compare
build/%.o: main/%.S | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
# The DSP's program: GNU binutils for the C6000, from their own image (tools/Dockerfile.c6x).
C6X_IMG ?= freedj-c6x-as
C6X     := docker run --rm -u $(shell id -u):$(shell id -g) -v $(CURDIR):/w -w /w $(C6X_IMG)
build/dsp.bin: dsp/audio.s | build
	$(C6X) sh -c "tic6x-elf-as -mlittle-endian -march=c67x+ dsp/audio.s -o build/dsp_audio.o && \
	  tic6x-elf-ld -EL -Ttext 0x10002000 -e _start build/dsp_audio.o -o build/dsp.elf && \
	  tic6x-elf-objcopy -O binary build/dsp.elf build/dsp.bin && \
	  tic6x-elf-nm build/dsp.elf | awk '\$$3 == \"loop\" { print \"0x\" \$$1 }' > build/dsp.idle"
build/dsp_blob.o: main/dsp_blob.S build/dsp.bin | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
# The display processor's program (gui/, GNU GCC for the Blackfin, from
# tools/Dockerfile.bfin) and its flash: an LDR stream and the boot splash.
BFIN_IMG ?= freedj-bfin-gcc
BFIN    := docker run --rm -u $(shell id -u):$(shell id -g) -v $(CURDIR):/w -w /w $(BFIN_IMG)
build/gui.elf: gui/gui.c gui/crt0.S gui/link.ld | build
	$(BFIN) sh -c "bfin-elf-gcc -mcpu=bf531 -O2 -ffreestanding -nostdlib -Wall -c gui/gui.c -o build/gui.o && \
	  bfin-elf-gcc -mcpu=bf531 -c gui/crt0.S -o build/gui_crt0.o && \
	  bfin-elf-ld -T gui/link.ld build/gui_crt0.o build/gui.o -o build/gui.elf"
build/splash.rgb555: tools/splash.py | build
	python3 tools/splash.py build/splash.rgb555 build/splash.png
build/gui.flash: build/gui.elf build/splash.rgb555 tools/mkflash.py
	python3 tools/mkflash.py build/gui.elf build/splash.rgb555 build/gui.flash
build/deck_event.o: ../src/deck_event.cpp ../src/deck_event.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
# The P4 deck's track stream (WAV, and MP3 through minimp3, MP3 only and no
# stdio, as the P4 builds it) and the shared WSOLA, unchanged.
MP3DEFS := -DMINIMP3_ONLY_MP3 -DMINIMP3_NO_STDIO -I../p4/deck/main/third_party/minimp3
build/stream.o: ../p4/deck/main/stream.cpp ../p4/deck/main/stream.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) $(MP3DEFS) -c $< -o $@
build/mp3.o: ../p4/deck/main/mp3_impl.c | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) $(MP3DEFS) -w -c $< -o $@
build/wsola.o: ../src/wsola.cpp ../src/wsola.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -DWSOLA_NATIVE -c $< -o $@
build/engine.o: main/engine.cpp main/*.h | build
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -DWSOLA_NATIVE $(MP3DEFS) -c $< -o $@
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
	docker build -t $(C6X_IMG) -f tools/Dockerfile.c6x tools
	docker build -t $(BFIN_IMG) -f tools/Dockerfile.bfin tools

emu: build/main.bin build/gui.flash
	tools/run_emu.sh build/main.bin

test: build/main.bin build/gui.flash
	python3 tools/test_splash.py build/main.bin
	python3 tools/test_panel.py build/main.bin
	python3 tools/test_link.py build/main.bin
	python3 tools/test_usb.py build/main.bin
	python3 tools/test_audio.py build/main.bin
	python3 tools/test_deck_link.py build/main.bin

clean:
	rm -rf build
.PHONY: all image emu test clean
