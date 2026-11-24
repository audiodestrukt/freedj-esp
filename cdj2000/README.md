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
| Ethernet | the SH7763's E-DMAC + EtherC at 0xFEF00000, an RTL8201CP PHY ([docs/ethernet.md](docs/ethernet.md)) | `main/netstack.cpp`, Link |
| USB | the SH7763's R8A66597-family host at 0xFE400000 ([docs/usb.md](docs/usb.md)) | `main/usb.cpp`, `main/fat.cpp`: the stick |
| DSP | TI C6727 (floating point) → WM8740 DAC | audio (later) |
| Display board | Analog Devices Blackfin BF531, 16 MB, draws 480×234 over its PPI | the screen (later) |
| Front panel | its own MCU, a serial report link to the main board ([docs/panel.md](docs/panel.md)) | `main/panel.*`: controls and lamps |

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
make test       # on the emulator: the panel, Pro DJ Link beside a played XDJ, a USB stick's tracks
python3 tools/demo.py   # see it: the panel app wired to it, its console, a fake XDJ, the stick
```

Shared with the ESP32 decks and compiled unchanged: `../src/deck_event.*`
(the deck events every input speaks), `../p4/deck/main/prolink.*` (Pro DJ
Link, on `shim/`: just enough ESP-IDF headers over our own network stack) and
`../p4/deck/main/rekordbox.*` (the stick's analysis, on stdio over our FAT).
Nothing outside `cdj2000/` is edited for this target.

`make emu` needs cdj-nxs2-qemu's `qemu-system-sh4` (its `setup.sh` builds
it; `CDJ_QEMU=` points at another). The image is loaded the way the deck's
bootloader leaves it: at the DRAM base, entered at 0xA4000800 (P2,
uncached), with SP at the top of DRAM.

## Milestones

1. **Boot** ✅ (2026-11-23): our image runs on the emulated main board and
   prints on both SCIFs (which one is the deck's console is not known yet).
2. **Panel** ✅ (2026-11-23): the front panel's keys, jog, select knob,
   tempo slider and DIRECTION lever become freedj-esp deck events; the
   lamps answer (PLAY, CUE, MASTER TEMPO, TEMPO RESET, for now from a
   stand-in transport). `make test` checks it end to end on the emulator.
3. **Link** ✅ (2026-11-23): the E-DMAC/EtherC driver and a small
   ARP/IPv4/UDP stack ([docs/ethernet.md](docs/ethernet.md)) carry
   freedj-esp's own Link code: the emulated CDJ-2000 announces itself as
   player 2, sends its status to the other players and follows the tempo
   master. ⚠ A made-up MAC and a link-local address for now.
4. **USB stick** ✅ (2026-11-23): the USB host, bulk-only mass storage, a
   FAT reader and stdio over it; freedj-esp's rekordbox scan lists the
   stick's analysed tracks with their tempo. ⚠ Two sectors per read for now.
5. **Audio**: our program on the DSP, feeding the DAC.
6. **Screen**: our program on the Blackfin, drawing the deck.
7. **Packaging**: an update file the deck's updater takes, and a checked way
   back to the stock firmware, before anything is flashed on a real deck.

⚠ Running on the emulator is the test until milestone 7; the emulators are
bring-up quality, so the code keeps to the peripherals and modes the stock
firmware uses wherever it can.
