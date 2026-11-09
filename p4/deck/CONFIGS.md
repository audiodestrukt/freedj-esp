# Configurations (plan)

Which combinations of board, screen, controls and audio freedj-esp can be built in, from
the P4 with a 4" screen down to a 0.96" OLED. Started 2026-11-08. Chips are compared in
[TARGETS.md](TARGETS.md); the release-1 controls and case are in [BUILDS.md](BUILDS.md).
Update the status column as each one runs, with its numbers.

## The axes

A configuration is one pick from each row. They are mostly independent: the audio core,
track loading and the deck logic don't care about the screen or where events come from.

| Axis | Options |
|---|---|
| Board | ESP32-P4 dev kit · ESP32-A1S Audio Kit · ESP32-S3, incl. the XIAO keychain (planned) · a custom board ([MAINBOARD.md](MAINBOARD.md)) |
| Screen | see *Screens* below, from 4" colour to 0.96" mono, or none |
| Controls | DJ2Go over USB MIDI · DJ2Go over the serial bridge (`tools/midi_bridge.py`) · AS5600 jog · panel buttons and pitch pot (`panel_gpio`) · quadrature encoders on PCNT, browse and jog (planned, see *Encoders*) · control board on I2C 0x42 (planned) · touch |
| Audio out | the board's codec (ES8311 on the P4, ES8388 on the A1S) · an I2S DAC such as a PCM5102 (planned) · USB audio interface (P4) |
| Tracks from | SD card · USB stick (needs USB host) · Pro DJ Link (needs a network; Ethernet on the P4) |
| Timecode in | off · codec line in (A1S, needs ~4 MB PSRAM for its table) · USB audio interface (P4) |

## What runs today

| Board | Screen | Status | Screen fps (waveform up) | Notes |
|---|---|---|---|---|
| P4 | 4" ST7796S 480×320 (MSP4021) | **runs**, the main build | 40–42 | `build/`, SPI 40 MHz |
| P4 | 2.8" ILI9341V 320×240 (MSP2834) | **runs** | not measured | `build28/`, `sdkconfig.defaults.screen28` |
| P4 | 2.8" layout on the 4" panel | **runs** | | `build28p/`, preview only |
| A1S | 2.8" or 3.2" ILI9341V 320×240 | **runs**, cut down | 7 | `build_a1s/`, SPI 4 MHz (header pins load-limited), no USB host, no Link, no timecode |

## Boards

What each board can carry. RAM decides the track window, which decides scratch and loops.

| Board | Chip | PSRAM | Fits | Status |
|---|---|---|---|---|
| Waveshare P4 dev kit | ESP32-P4, 2× RISC-V 360 MHz | 32 MB | everything, any tier | main build |
| AI-Thinker A1S Audio Kit | ESP32 (D0WD v3.0) | 4 MB mapped | Full tier cut down (2 MB window, ~12 s); Minimal on an I2C OLED | runs |
| **Seeed XIAO ESP32S3**: the keychain | ESP32-S3, 2× LX7 240 MHz, FPU | 8 MB (check the board) | Minimal or Compact tier with an I2S DAC; USB host for the DJ2Go; LiPo charger on board. About 21 × 18 mm | planned, after the S3 port; **not on hand** (the XIAO here is a C6) |
| NodeMCU-32S | ESP32 (D0WDQ6 **v1.0**) | **none** (probe, 2026-11-08) | Minimal tier only: SD playback with pitch, cue and hot cues (cue jumps re-read the card), OLED, EC11, PCM5102. No long loops, no real back-scratch; keylock unknown. Clean header pins, so SPI screens should run fast | **dead end**: the A1S's chip without its DAC or PSRAM |

NodeMCU-32S, measured with a probe image: 4 MB flash, PSRAM not found (`PSRAM ID read error:
0xffffffff`), **358 KB internal RAM free with an empty program, largest block 168 KB**. That's
under a second of 44.1 kHz stereo, before the deck's own tasks, I2S DMA and the SD driver.
Not pursued (2026-11-08): it's the A1S's chip without the codec or the PSRAM, so it adds nothing.
A board with both, built for the deck, is [MAINBOARD.md](MAINBOARD.md).

**Out of scope: freedj-nano.** The smallest decks (an ESP32-C6, or a chip even smaller, with PWM
or bit-banged audio) belong to a separate project, freedj-nano (`../freedj-nano`, started 2026-11-08: C6, fixed-point keylock, u8g2 OLEDs and the SPI TFTs). They need a
fixed-point audio path (no FPU), single-core scheduling and no PSRAM, so they wouldn't share
this firmware's audio core; at most the deck events and the DJ2Go decoder from `src/`. Boards
on hand for it: a Seeed XIAO ESP32C6 and a DFRobot Beetle ESP32-C6.

## Screens

What a full redraw costs on the wire decides which UI fits. The waveform scrolls, so its
band is resent in full every frame.

| Class | Typical parts | Pixels | Bytes per full frame | Bus | UI it can carry |
|---|---|---|---|---|---|
| Large colour | 4" ST7796S, 3.5" ILI9488 | 480×320 | 307 KB (460 KB for the ILI9488, which takes only 18-bit colour over SPI) | SPI 40 MHz, or MIPI-DSI later | **Full**: today's 4" layout |
| Medium colour | 2.4–3.2" ILI9341, 2.0" ST7789 | 320×240 | 154 KB | SPI | **Full**: today's 2.8" layout |
| Small square colour | 1.3–1.54" ST7789 (240×240), 1.69" (240×280) | 240×240 | 115 KB | SPI | **Compact**: waveform, time, BPM, state; a short list |
| Strip colour | 0.96" ST7735 (160×80), 1.14" ST7789 (135×240) | 160×80 | 26 KB | SPI | **Compact**, landscape strip: waveform with time and BPM over it |
| Mono OLED | 0.96" SSD1306, 1.3" SH1106 | 128×64 | 1 KB | I2C 400 kHz, or SPI | **Minimal**: 1-bit waveform, time, track name; list by encoder |
| Tiny mono OLED | 0.91" SSD1306 | 128×32 | 0.5 KB | I2C | **Minimal**, one line plus a waveform strip |
| None | | | | | **Headless**: state on LEDs, track select by encoder (as the old A1S firmware) |

What this means:
- **The small screens suit the slow boards.** A 160×80 waveform band (160×40) is about 13 KB, ~26 ms at the A1S's 4 MHz, against ~95 ms for today's 320×74 band.
- **An I2C OLED on the A1S needs no header pins.** It goes on the codec's bus (33/32) at 0x3C, next to the codec (0x10), the AS5600 (0x36) and touch (0x38). A full frame holds the bus for ~23 ms at 400 kHz, so the AS5600 reads have to interleave with it. Measure the jog's update rate when trying it.
- **Mono and colour need different drawing.** The waveform is drawn straight into an RGB565 canvas today. A 1-bit screen needs its own waveform drawing (peaks as on/off pixels, no colour bands). LVGL 9 and `esp_lvgl_port` do drive SSD1306 panels in 1-bit.
- **Small screens rarely have touch**, and are too small to tap anyway. They need the encoder, or buttons, for the list and the pads.

## UI tiers

Three layouts, picked by the screen class, not one per panel:

| Tier | Screens | On screen | Needs from the controls |
|---|---|---|---|
| **Full** | 320×240 and up | today's UI: header, overview, waveform, meter, list, pads | touch, or the DJ2Go |
| **Compact** | 160×80 to 240×280 | waveform, time, BPM, play/cue state; list as a page of its own | encoder or DJ2Go for browse; no pads on screen |
| **Minimal** | 128×64 and 128×32 mono | track name, time, 1-bit waveform strip | encoder for browse |
| **Headless** | none | | encoder, LEDs for play/cue |

## Encoders (for the small builds)

Below the Full tier there's no touch, so browsing needs a knob. The jog can come off I2C too.

**How they connect:** a quadrature encoder's A and B lines go to two GPIOs (plus one for a
push switch). The ESP32's hardware pulse counter (PCNT, on every ESP32 including the A1S's,
the S3 and the P4) decodes the quadrature and has a glitch filter. That means no I2C, no
interrupts and no CPU cost. The old A1S firmware read its EC11 with pin interrupts
(`src/controls.cpp`); PCNT replaces that.

| Encoder | Counts per turn | Good for | Cost | Notes |
|---|---|---|---|---|
| **EC11** mechanical, with push | 20–24 detents (×4 edges decoded, used per detent) | browse, load (push), nudge, menus | ~$1 | Contacts bounce: PCNT's glitch filter (up to ~12 µs) handles most of it; add 10 nF per line if it still skips |
| **Optical**, 100–600 PPR | 400–2400 decoded | **jog and scratch** (the AS5600 gives 4096) | ~$10–20 | Often 5 V open-collector: power per the part, pull A/B up to 3V3 only (never 5 V into a GPIO) |
| AS5600 (today) | 4096 | jog and scratch | ~$2 | On I2C: shares the bus with the codec, touch and an I2C OLED |

**Recommended for the Minimal and Compact builds:** an EC11 for browse and push, plus an
optical encoder for the jog, both on PCNT. Taking the jog off I2C means an OLED's ~23 ms
frames on the same bus can't delay it. If cost matters most, keep the AS5600 and measure
its update rate while the OLED draws.

**Pins on the A1S.** With an I2C OLED instead of the SPI screen, the screen's header pins are
free again. The old firmware's map works as it is: **EC11 A 19, B 23, push 18**. An optical jog
takes two more, for example **21 and 22** (the amp enable on 21 stays off, as with the SPI
screen). PLAY and CUE stay on KEY1/KEY2 (36/13). The header pins' board load doesn't matter
here: an encoder switches thousands of times slower than SPI.

**Firmware:**
- One `encoder.cpp`: a PCNT unit per encoder, in quadrature (×4) mode, with the glitch filter. The jog task polls the counts at 1 kHz, as it reads the AS5600 today.
- The EC11 sends `deck::Event`s: turn → list up/down, push → load, long push → back. Same events the DJ2Go's browse knob sends.
- The optical jog feeds the same jog path as the AS5600 (ticks per turn from its PPR), so scratch and nudge feel the same. Its counts per turn go in `board_*.h` next to the pins.
- At boot: if the AS5600 answers on I2C, use it for the jog. Otherwise use the optical encoder if pins are set.

## How the firmware would carry them

- **Layered sdkconfig fragments**, as `build28/` already does (`sdkconfig.defaults;sdkconfig.defaults.screen28`). One fragment per board (`sdkconfig.defaults.a1s`, later `.s3`) and one per screen. A configuration is a board fragment plus a screen fragment, built into its own directory.
- **Kconfig:** extend the `FREEDJ_SCREEN` choice with each panel. Each one selects its tier (`FREEDJ_UI_FULL`, `_COMPACT`, `_MINIMAL`, `_HEADLESS`), its driver and its bus. Board-only tuning gets a board flag (for example `FREEDJ_BOARD_A1S`), not a chip one.
- **Pins stay in `board_*.h`.** A screen on I2C uses the board's existing I2C bus; one on SPI uses the board's screen pins.
- **`ui.cpp` splits by tier.** The deck state (`ui::State`) and `publish()` stay shared. Each tier gets its own layout and drawing file (`ui_full.cpp`, `ui_compact.cpp`, `ui_mono.cpp`), and only one is built in.
- **`display.cpp` splits by bus:** SPI colour panels (today), I2C or SPI mono OLEDs, and later MIPI-DSI.
- **A build-all script** (or CI matrix) builds every listed configuration, so a change to the shared code can't quietly break one. It records each image size for the changelog.

## Order of work

1. **Encoders on PCNT** (see *Encoders*): an EC11 for browse first, since every tier below Full needs it. Then an optical jog, measured against the AS5600.
2. **0.96" SSD1306 on the A1S**, on the codec's I2C bus: the Minimal tier and the mono display path. It's the cheapest screen and needs no new pins.
3. **0.96" ST7735 160×80 on the A1S** (SPI, same pins as today): the Compact tier.
4. **1.54" ST7789 240×240**, to check that Compact scales to a square screen.
5. **Build-all script**, once there are four or more configurations.
6. **ESP32-S3** board fragment ([TARGETS.md](TARGETS.md)), with every screen above. Then the XIAO ESP32S3 keychain: Minimal tier, EC11, a small I2S DAC, battery.

## Open questions

- Is the P4 2.8" frame rate worth measuring to fill in the table? (Probably same as the 4", less the pixels.)
- Does a Compact layout want the overview bar (track position), or does the time cover it?
- On the Minimal tier, which matters more in 128×64: the waveform strip or a larger time readout?
- Which optical encoder: PPR, shaft and body size against the jog's bearing stack, and whether it runs at 3.3 V.
- I2C OLED speed: many SSD1306 modules run at 1 MHz despite the 400 kHz rating, but the codec and the AS5600 on the same bus may not.
