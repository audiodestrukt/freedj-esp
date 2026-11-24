# freedj on a stock CDJ-2000 (experiment)

A replacement firmware for an unmodified Pioneer CDJ-2000, built against the
emulated deck in [cdj-nxs2-qemu](https://github.com/Stijn-Jacobs/cdj-nxs2-qemu)
(and [cdj2000-emulator](https://github.com/cdj2k-revival/cdj2000-emulator)
as a cross-check). An experiment beside the ESP32 decks: it may borrow code
from `../src` and `../p4`, but nothing here changes those builds.

## The hardware, as the emulators model it

| Part | What it is | Ours |
|---|---|---|
| Main board | Renesas SH7763 (SH-4A, FPU), 64 MB SDRAM at 0x04000000, 4 MB NOR flash, NORTi RTOS stock | `main/`: the deck, Link, USB, the panel |
| DSP | TI C6727 (floating point) → WM8740 DAC | audio (later) |
| Display board | Analog Devices Blackfin BF531, 16 MB, draws 480×234 over its PPI | the screen (later) |
| Front panel | its own MCU, a serial report link to the main board | controls and lamps (milestone 2) |

The hardware reference is the emulators' QEMU board models and docs,
together with the chips' public manuals (Renesas SH7763, TI C6727, Wolfson
WM8740). We never read Pioneer's firmware, its unpacked images, or the
emulator repositories' firmware analysis, and copy no emulator code here.

## Build and run

The SH-4 compiler comes in a Docker image, so the host needs only Docker:

```sh
cd cdj2000
make image      # once: the sh4-linux-gnu GCC image (tools/Dockerfile)
make            # build/main.bin: a flat image for the main board
make emu        # run it on the emulated CDJ-2000; SCIF0 on stdout, SCIF1 in build/scif1.log
```

`make emu` needs cdj-nxs2-qemu's `qemu-system-sh4` (its `setup.sh` builds
it; `CDJ_QEMU=` points at another). The image is loaded the way the deck's
bootloader leaves it: at the DRAM base, entered at 0xA4000800 (P2,
uncached), with SP at the top of DRAM.

## Milestones

1. **Boot** ✅ (2026-11-23): our image runs on the emulated main board and
   prints on both SCIFs (which one is the deck's console is not known yet).
2. **Panel**: read the keys, the jog and the tempo slider; light the lamps.
3. **Link**: the EtherC up; announce, beats and status as a modern player.
4. **USB stick**: FAT, rekordbox's export.pdb, load a track.
5. **Audio**: our program on the DSP, feeding the DAC.
6. **Screen**: our program on the Blackfin, drawing the deck.
7. **Packaging**: an update file the deck's updater takes, and a checked way
   back to the stock firmware, before anything is flashed on a real deck.

⚠ Running on the emulator is the test until milestone 7; the emulators are
bring-up quality, so the code keeps to the peripherals and modes the stock
firmware uses wherever it can.
