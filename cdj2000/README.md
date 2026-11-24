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
| DSP | TI C6727 (floating point) → WM8740 DAC ([docs/audio.md](docs/audio.md)) | `dsp/audio.s` (ours, GNU binutils), `main/dsp.cpp`, `main/player.cpp` |
| Display board | Analog Devices Blackfin BF531, 16 MB, 2 MB flash, draws 480×234 over its PPI ([docs/screen.md](docs/screen.md)) | `gui/` (GNU GCC for the Blackfin), its flash from `tools/mkflash.py` |
| Front panel | its own MCU, a serial report link to the main board ([docs/panel.md](docs/panel.md)) | `main/panel.*`: controls and lamps |

The hardware reference is the emulators' QEMU board models and docs,
together with the chips' public manuals (Renesas SH7763, TI C6727, Wolfson
WM8740). We never read Pioneer's firmware, its unpacked images, or the
emulator repositories' firmware analysis, and copy no emulator code here.

## The second board: the XDJ-1000MK2

The same image builds for the XDJ-1000MK2 (`make BOARD=xdj1000mk2`, into
`build-xdj1000mk2/`), against cdj-nxs2-qemu's `xdj1000mk2` machine. Its
main CPU is an SH7724, also SH-4A; `main/board.h` holds what differs:

| | CDJ-2000 | XDJ-1000MK2 |
|---|---|---|
| DRAM, entry | 64 MB at 0x04000000, 0xA4000800 | 256 MB at 0x08000000, 0xA8000800 |
| Peripheral clock (TMU0 at a quarter) | 54 MHz | 41.67 MHz |
| Ethernet | 0xFEF00000, LMON 0 = link | 0xA4600000 (the SH7724's), LMON 1 = link |
| USB stick | 0xFE400000 | 0xA4D90000 (the second of two blocks) |
| Panel frame | 24 bytes | 32 bytes, the same sync and checksum |
| DSP | C6727: our program | C6747: not ported, a silent sink at 44.1 kHz (`main/dsp_null.cpp`) |
| Screen | the Blackfin: our program, 480×234 RGB555 | the SH7724's own LCDC: the main board draws the same screen (`common/deck_draw.c`) at 800×480 RGB565, fonts baked 1.7× ([docs/screen_xdj1000mk2.png](docs/screen_xdj1000mk2.png)) |

`make test BOARD=xdj1000mk2` (2026-11-24): the panel (PLAY, CUE, MASTER
TEMPO, jog, select knob, tempo slider), Link beside a played XDJ, the USB
stick, the deck's beat packets at +0 and +1.25 %, NFS, freedj-3000's
client and the screen (drawn, scrolling while playing, still when paused)
all pass. ⚠ The tempo slider is read against mid travel because the
emulator gives the XDJ no centre reference; the lever can only be checked
at FWD there; the LCDC is set only as far as the emulator reads it (a
panel's clock, pins and timings are to set on a deck). Next: the C6747's
audio. Nothing has
run on a real XDJ.

## Build and run

The SH-4 compiler comes in a Docker image, so the host needs only Docker:

```sh
cd cdj2000
make image      # once: the toolchain images (tools/): SH-4 GCC, C6000 binutils, Blackfin GCC
make            # build/main.bin for the main board, build/gui.flash for the display board
make emu        # run it on the emulated CDJ-2000; SCIF0 on stdout, SCIF1 in build/scif1.log
make test       # on the emulator: the panel, Pro DJ Link beside a played XDJ, a USB stick's tracks, audio
python3 tools/demo.py   # see it: the panel app wired to it, its console, a fake XDJ, the stick
```

Shared with the ESP32 decks and compiled unchanged: `../src/deck_event.*`
(the deck events every input speaks), `../p4/deck/main/prolink.*` (Pro DJ
Link, on `shim/`: just enough ESP-IDF headers over our own network stack),
`../p4/deck/main/rekordbox.*` (the stick's analysis, on stdio over our FAT),
`../p4/deck/main/stream.*` with minimp3 (WAV and MP3 decoded into a window
round the playhead) and `../src/wsola.*` (MASTER TEMPO). The deck's
transport (`main/engine.cpp`) is this target's own, written after the P4's.
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
5. **Audio** ✅ (2026-11-23): our own DSP program (C6000 assembly) streams
   a ring of PCM to the DAC through the dMAX; the main board boots it and
   feeds it. Real time and bit exact on the emulator. ⚠ The McASP's clocks
   and format are not set up yet (the emulator doesn't need them; a real
   deck will).
   - **The deck engine** ✅ (2026-11-23, [docs/engine.md](docs/engine.md)):
     WAV and MP3 from the stick through freedj-esp's track stream; CUE as on
     a CDJ, hot cues, loops, memory cue call, the tempo slider and its
     ranges, TEMPO RESET, MASTER TEMPO (WSOLA), the jog (bend, scratch),
     the DIRECTION lever; the select knob and its push browse and load. On
     Link it is a modern player: beat packets on the heard beat, its tempo
     and place in the bar (an original CDJ-2000 sends none of that).
   - **The stick on Link** ✅ (2026-11-24): the status names the loaded
     track by its rekordbox ID (from the stick's `export.pdb`), and the
     stick is served read-only over NFS as a player serves its USB (portmap
     111, mount + NFSv2 on 2049, `/C/`), so freedj-3000's FOLLOW (and so
     Overdeck's `follow = N`) loads the track this deck is playing. No
     dbserver yet (it needs TCP): a stock CDJ can't browse the stick, and
     a TCP connection is reset at once.
6. **Screen**: our program on the Blackfin, drawing the deck.
   - **The boot splash** ✅ (2026-11-23): the display board boots our
     program from its flash and shows audiodestrukt's splash
     ([docs/splash.png](docs/splash.png)) from the first frame; pixel exact
     on the emulator.
   - **The deck's screen** ✅ (2026-11-24, [docs/screen_playing.png](docs/screen_playing.png)):
     the Blackfin draws it from what the main board sends (the link is too
     slow for frames): a scrolling waveform round the playhead with the
     grid, cue, hot cues and loop; the overview; REMAIN, the beat in the
     bar, TEMPO, BPM; the title. 30 frames a second, double-buffered.
7. **Packaging**: an update file the deck's updater takes, and a checked way
   back to the stock firmware, before anything is flashed on a real deck.

⚠ Running on the emulator is the test until milestone 7; the emulators are
bring-up quality, so the code keeps to the peripherals and modes the stock
firmware uses wherever it can.
