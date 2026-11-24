# The CDJ-2000 main board image (SH7763, SH-4A, little-endian).  The compiler
# comes from a Docker image (tools/Dockerfile), so the host needs only Docker:
#   make            build build/main.bin
#   make emu        run it on cdj-nxs2-qemu's emulated CDJ-2000 (console on stdout)
IMG     ?= freedj-sh4-gcc
DOCKER  := docker run --rm -u $(shell id -u):$(shell id -g) -v $(CURDIR):/w -w /w $(IMG)
CROSS   := sh4-linux-gnu-
CFLAGS  := -m4a -ml -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -Wall -Wextra -std=c11 -Imain
OBJS    := build/start.o build/main.o build/scif.o

all: build/main.bin

build/%.o: main/%.c main/*.h | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
build/%.o: main/%.S | build
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -c $< -o $@
build/main.elf: $(OBJS) link.ld
	$(DOCKER) $(CROSS)gcc $(CFLAGS) -T link.ld -Wl,--build-id=none $(OBJS) -lgcc -o $@
build/main.bin: build/main.elf
	$(DOCKER) $(CROSS)objcopy -O binary $< $@
build:
	mkdir -p build

image:
	docker build -t $(IMG) tools

emu: build/main.bin
	tools/run_emu.sh build/main.bin

clean:
	rm -rf build
.PHONY: all image emu clean
