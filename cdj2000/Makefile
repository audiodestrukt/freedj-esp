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
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wall -Wextra -Imain -Ishim -Icommon -I../src -I../p4/deck/main
CFLAGS  := $(ARCH) -ffreestanding -std=c11
# C++ is compiled hosted (libstdc++'s headers refuse -ffreestanding) but links
# nothing from the system: main/runtime.cpp gives it a heap and its error
# hooks, and instantiates std::string here instead of taking it from libstdc++.
CXXFLAGS := $(ARCH) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
# BOARD=cdj2000 (the default, into build/) or xdj1000mk2 (build-xdj1000mk2/):
# main/board.h holds what differs.
BOARD   ?= cdj2000
ifeq ($(BOARD),cdj2000)
B       := build
BOARD_OBJS := $(B)/dsp.o $(B)/dsp_blob.o $(B)/screen.o
LINK_ORG := 0xA4000000
LINK_ENT := 0xA4000800
LINK_LEN := 64M
EXTRA   := build/gui.flash
else ifeq ($(BOARD),xdj1000mk2)
B       := build-xdj1000mk2
ARCH    += -DBOARD_XDJ1000MK2
CFLAGS  += -DBOARD_XDJ1000MK2
CXXFLAGS += -DBOARD_XDJ1000MK2
BOARD_OBJS := $(B)/dsp_null.o $(B)/screen_null.o
LINK_ORG := 0xA8000000
LINK_ENT := 0xA8000800
LINK_LEN := 256M
EXTRA   :=
else
$(error BOARD is cdj2000 or xdj1000mk2)
endif
OBJS    := $(B)/start.o $(B)/main.o $(B)/scif.o $(B)/tmu.o $(B)/libc.o $(B)/printf.o $(B)/panel.o \
           $(B)/coop.o $(B)/coop_switch.o $(B)/netstack.o $(B)/usb.o $(B)/fat.o $(B)/files.o $(B)/runtime.o \
           $(BOARD_OBJS) $(B)/engine.o \
           $(B)/stream.o $(B)/wsola.o $(B)/mp3.o \
           $(B)/deck_event.o $(B)/prolink.o $(B)/rekordbox.o $(B)/pdb.o $(B)/nfsd.o

all: $(B)/main.bin $(EXTRA)

$(B)/%.o: main/%.c main/*.h | $(B)
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/%.o: main/%.cpp main/*.h common/*.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
# glibc's headers mark FILE* and DIR* arguments nonnull; ours check anyway.
$(B)/files.o: CXXFLAGS += -Wno-nonnull-compare
$(B)/%.o: main/%.S | $(B)
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
# The DSP's program: GNU binutils for the C6000, from their own image (tools/Dockerfile.c6x).
C6X_IMG ?= freedj-c6x-as
C6X     := docker run --rm -u $(shell id -u):$(shell id -g) -v $(CURDIR):/w -w /w $(C6X_IMG)
build/dsp.bin: dsp/audio.s | build
	$(C6X) sh -c "tic6x-elf-as -mlittle-endian -march=c67x+ dsp/audio.s -o build/dsp_audio.o && \
	  tic6x-elf-ld -EL -Ttext 0x10002000 -e _start build/dsp_audio.o -o build/dsp.elf && \
	  tic6x-elf-objcopy -O binary build/dsp.elf build/dsp.bin && \
	  tic6x-elf-nm build/dsp.elf | awk '\$$3 == \"loop\" { print \"0x\" \$$1 }' > build/dsp.idle"
$(B)/dsp_blob.o: main/dsp_blob.S build/dsp.bin | $(B)
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
# The display processor's program (gui/, GNU GCC for the Blackfin, from
# tools/Dockerfile.bfin) and its flash: an LDR stream and the boot splash.
BFIN_IMG ?= freedj-bfin-gcc
BFIN    := docker run --rm -u $(shell id -u):$(shell id -g) -v $(CURDIR):/w -w /w $(BFIN_IMG)
build/font.h: tools/font.py | build
	python3 tools/font.py build/font.h
build/gui.elf: gui/gui.c gui/crt0.S gui/link.ld common/screen_link.h build/font.h | build
	$(BFIN) sh -c "bfin-elf-gcc -mcpu=bf531 -O2 -ffreestanding -nostdlib -Wall -Icommon -Ibuild -c gui/gui.c -o build/gui.o && \
	  bfin-elf-gcc -mcpu=bf531 -c gui/crt0.S -o build/gui_crt0.o && \
	  bfin-elf-gcc -mcpu=bf531 -nostdlib -T gui/link.ld build/gui_crt0.o build/gui.o -lgcc -o build/gui.elf"
build/splash.rgb555: tools/splash.py | build
	python3 tools/splash.py build/splash.rgb555 build/splash.png
build/gui.flash: build/gui.elf build/splash.rgb555 tools/mkflash.py
	python3 tools/mkflash.py build/gui.elf build/splash.rgb555 build/gui.flash
$(B)/deck_event.o: ../src/deck_event.cpp ../src/deck_event.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
# The P4 deck's track stream (WAV, and MP3 through minimp3, MP3 only and no
# stdio, as the P4 builds it) and the shared WSOLA, unchanged.
MP3DEFS := -DMINIMP3_ONLY_MP3 -DMINIMP3_NO_STDIO -I../p4/deck/main/third_party/minimp3
$(B)/stream.o: ../p4/deck/main/stream.cpp ../p4/deck/main/stream.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) $(MP3DEFS) -c $< -o $@
$(B)/mp3.o: ../p4/deck/main/mp3_impl.c | $(B)
	$(DOCKER) $(CROSS)gcc $(CFLAGS) $(MP3DEFS) -w -c $< -o $@
$(B)/wsola.o: ../src/wsola.cpp ../src/wsola.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -DWSOLA_NATIVE -c $< -o $@
$(B)/engine.o: main/engine.cpp main/*.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -DWSOLA_NATIVE $(MP3DEFS) -c $< -o $@
$(B)/rekordbox.o: ../p4/deck/main/rekordbox.cpp ../p4/deck/main/rekordbox.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -c $< -o $@
$(B)/prolink.o: ../p4/deck/main/prolink.cpp ../p4/deck/main/prolink.h shim/*.h shim/*/*.h main/*.h | $(B)
	$(DOCKER) $(CROSS)g++ $(CXXFLAGS) -Wno-unused-parameter -c $< -o $@
# The link script at this board's DRAM base (link.ld is the CDJ-2000's).
$(B)/link.ld: link.ld Makefile | $(B)
	sed -e 's/0xA4000800/$(LINK_ENT)/' -e 's/0xA4000000/$(LINK_ORG)/g' -e 's/LENGTH = 64M/LENGTH = $(LINK_LEN)/' $< > $@
$(B)/main.elf: $(OBJS) $(B)/link.ld
	$(DOCKER) $(CROSS)gcc $(ARCH) -T $(B)/link.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments $(OBJS) -lgcc -o $@
$(B)/main.bin: $(B)/main.elf
	$(DOCKER) $(CROSS)objcopy -O binary $< $@
$(B):
	mkdir -p $(B)
ifneq ($(B),build)
build:
	mkdir -p build
endif

image:
	docker build -t $(IMG) tools
	docker build -t $(C6X_IMG) -f tools/Dockerfile.c6x tools
	docker build -t $(BFIN_IMG) -f tools/Dockerfile.bfin tools

emu: $(B)/main.bin build/gui.flash
	tools/run_emu.sh $(B)/main.bin

# The CDJ-2000 runs every test; the XDJ-1000MK2 those that need no DSP or
# Blackfin program (its audio and screen are not ported yet).
ifeq ($(BOARD),cdj2000)
test: $(B)/main.bin build/gui.flash
	python3 tools/test_splash.py $(B)/main.bin
	python3 tools/test_panel.py $(B)/main.bin
	python3 tools/test_link.py $(B)/main.bin
	python3 tools/test_usb.py $(B)/main.bin
	python3 tools/test_audio.py $(B)/main.bin
	python3 tools/test_deck_link.py $(B)/main.bin
	python3 tools/test_screen.py $(B)/main.bin
	python3 tools/test_nfs.py $(B)/main.bin
	python3 tools/test_nfs_freedj.py $(B)/main.bin
else
test: $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_panel.py $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_link.py $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_usb.py $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_deck_link.py $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_nfs.py $(B)/main.bin
	BOARD=$(BOARD) python3 tools/test_nfs_freedj.py $(B)/main.bin
endif

clean:
	rm -rf build
.PHONY: all image emu test clean
