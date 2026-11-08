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
| Board | ESP32-P4 dev kit · ESP32-A1S Audio Kit · ESP32-S3 (planned) |
| Screen | see *Screens* below, from 4" colour to 0.96" mono, or none |
| Controls | DJ2Go over USB MIDI · DJ2Go over the serial bridge (`tools/midi_bridge.py`) · AS5600 jog · panel buttons and pitch pot (`panel_gpio`) · rotary encoder (planned) · control board on I2C 0x42 (planned) · touch |
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

## How the firmware would carry them

- **Layered sdkconfig fragments**, as `build28/` already does (`sdkconfig.defaults;sdkconfig.defaults.screen28`). One fragment per board (`sdkconfig.defaults.a1s`, later `.s3`) and one per screen. A configuration is a board fragment plus a screen fragment, built into its own directory.
- **Kconfig:** extend the `FREEDJ_SCREEN` choice with each panel. Each one selects its tier (`FREEDJ_UI_FULL`, `_COMPACT`, `_MINIMAL`, `_HEADLESS`), its driver and its bus. Board-only tuning gets a board flag (for example `FREEDJ_BOARD_A1S`), not a chip one.
- **Pins stay in `board_*.h`.** A screen on I2C uses the board's existing I2C bus; one on SPI uses the board's screen pins.
- **`ui.cpp` splits by tier.** The deck state (`ui::State`) and `publish()` stay shared. Each tier gets its own layout and drawing file (`ui_full.cpp`, `ui_compact.cpp`, `ui_mono.cpp`), and only one is built in.
- **`display.cpp` splits by bus:** SPI colour panels (today), I2C or SPI mono OLEDs, and later MIPI-DSI.
- **A build-all script** (or CI matrix) builds every listed configuration, so a change to the shared code can't quietly break one. It records each image size for the changelog.

## Order of work

1. **Encoder input** (an EC11 on two GPIOs plus its push, or the AS5600 in a "browse" mode). Every tier below Full needs it.
2. **0.96" SSD1306 on the A1S**, on the codec's I2C bus: the Minimal tier and the mono display path. It's the cheapest screen and needs no new pins.
3. **0.96" ST7735 160×80 on the A1S** (SPI, same pins as today): the Compact tier.
4. **1.54" ST7789 240×240**, to check that Compact scales to a square screen.
5. **Build-all script**, once there are four or more configurations.
6. **ESP32-S3** board fragment ([TARGETS.md](TARGETS.md)), with every screen above.

## Open questions

- Is the P4 2.8" frame rate worth measuring to fill in the table? (Probably same as the 4", less the pixels.)
- Does a Compact layout want the overview bar (track position), or does the time cover it?
- On the Minimal tier, which matters more in 128×64: the waveform strip or a larger time readout?
- I2C OLED speed: many SSD1306 modules run at 1 MHz despite the 400 kHz rating, but the codec and the AS5600 on the same bus may not.
