# P4 deck

A small CDJ-style deck on the **Waveshare ESP32-P4-Module-DEV-KIT**. It plays WAV and MP3 from the SD card. You control it with a Numark DJ2Go over USB, the touch screen, or serial keys. It has varispeed and keylock, CDJ-style CUE, and a scrolling waveform.

This lives next to the A1S firmware in the repo root and doesn't replace it. The timecode vinyl path is still the A1S build. Both builds share the deck event types, the DJ2Go decoder and WSOLA, all from `../../src`.

## Hardware

| Part | Notes |
|---|---|
| Board | Waveshare ESP32-P4-Module-DEV-KIT. ESP32-P4 rev 1.3 (needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`), 2× RISC-V 360 MHz, 32 MB PSRAM, 16 MB flash |
| Audio out | On-board ES8311 codec (I2C 0x18) → NS4150B amp → speaker. Mono. Amp enable is GPIO 53 |
| SD | On-board slot, 4-bit SDMMC, 40 MHz, powered from internal LDO channel 4 |
| Controller | Numark DJ2Go in any of the four USB-A ports (USB host, class-compliant MIDI) |
| Screen (stand-in) | LCDwiki **MSP4021**: 4" 480×320, ST7796S over SPI + FT6336U capacitive touch. Will be replaced by a MIPI-DSI screen |
| Serial | USB-C, CH343 → `/dev/ttyACM0`, console UART0 (GPIO 37/38) |

### Wiring the MSP4021

Header pin numbers follow the Raspberry Pi convention. Pin 1 is **3V3**, at the end next to the C6_UART connector. The column that starts with two **5V** pins is the even (right-hand) column.

| Screen pin | Header pin | GPIO | Notes |
|---|---|---|---|
| VCC | 17 | 3V3 | **3V3 only. Never connect it to 5V as well.** |
| GND | 20 | GND | |
| LCD_CS | 15 | 6 | |
| LCD_RST | 18 | 4 | |
| LCD_RS (D/C) | 16 | 5 | |
| SDI (MOSI) | 13 | 20 | |
| SCK | 11 | 21 | 40 MHz on ~15 cm jumpers |
| LED | 19 | 3 | Backlight, on/off |
| SDO (MISO) | — | — | Not connected (see *Vsync* below) |
| CTP_SCL | 5 | 8 | Shared with the codec's I2C bus |
| CTP_SDA | 3 | 7 | Shared with the codec's I2C bus |
| CTP_RST | 21 | 2 | |
| CTP_INT | 7 | 23 | Wired but **not used** (see below) |
| SD_CS | — | — | The screen's own SD slot; not used |

**Two wiring mistakes that look like a dead board.** In both, the CH343 still enumerates, so the port is there, but the P4 prints nothing and esptool can't connect:
- **Touch I2C on TXD/RXD** (pins 8/10, GPIO 37/38) instead of SDA/SCL (pins 3/5). That puts the touch chip on the serial console.
- **Screen VCC to both 3V3 and 5V.** That pushes 5 V into the 3.3 V rail.

## Build and flash

The build runs in the ESP-IDF v5.5 Docker image, mounting the repo root (the build reaches into `../../src`):

```
cd <repo root>
docker run --rm -v $PWD:/repo -w /repo/p4/deck espressif/idf:release-v5.5 idf.py build
docker run --rm -v $PWD/p4/deck:/d alpine chown -R $(id -u):$(id -g) /d   # component manager runs as root
cd p4/deck/build && esptool --chip esp32p4 --port /dev/ttyACM0 -b 921600 write-flash @flash_args
```

After changing `sdkconfig.defaults`, delete `sdkconfig` so the defaults are applied again.

## Controls

**DJ2Go (deck A):**
- **Browse knob:** move through the track list.
- **LOAD A:** load the selected track.
- **PLAY:** play or pause.
- **CUE:** CDJ-style.
  - While playing: jump back to the cue point and pause.
  - While paused: set the cue point here, play while held, return on release.
- **Pitch fader:** ±8 %.
- **Jog:** nudge while playing, scrub while paused.
- **SYNC:** keylock on/off, until there's a dedicated button.
- **Channel volume:** level.

**Touch:**
- Tap a row to select it; tap it again, or press LOAD, to load it.
- The CUE, PLAY and LOAD buttons on screen behave like the DJ2Go's.
- Tap the waveform to bring the list back.

**Serial keys** (console UART), for hands-off testing:

| Key | Action |
|---|---|
| `1`–`9` | select track |
| `l` | load |
| `p` | play/pause |
| `c` | cue (press + release) |
| `k` | keylock |
| `w` | cycle the waveform test mode: normal → drawn but not sent to the screen → off |

[`tools/p4_serial.py`](../../tools/p4_serial.py) holds the port open without resetting the board, logs to a file, and sends anything appended to `<log>.cmd`. A whole experiment can be scripted, for example:

```
tools/p4_serial.py deck.log &
echo -n 3l >> deck.log.cmd; sleep 3; echo -n p >> deck.log.cmd
```

## How it's put together

Core 1 is reserved for audio. Everything else runs on core 0, and core 0 is kept lightly loaded so there's room for Pro DJ Link later.

| Task | Core | Prio | Job |
|---|---|---|---|
| `audio` | 1 | 10 | Renders 256-frame blocks (5.8 ms): varispeed (linear interpolation) or keylock (resample to 44.1 kHz, then WSOLA). Writes to I2S and publishes UI state |
| `loader` | 0 | 4 | Owns the SD card: opens tracks and keeps the PSRAM window filled |
| `taskLVGL` | 0 | 3 | esp_lvgl_port: UI timers, rendering, SPI flushes |
| `usb_host`, `usb_midi` | any | 6, 5 | USB host library, DJ2Go MIDI decoding |
| `keys` | 0 | 2 | Serial keys |
| `report` | 0 | 1 | Prints the 2 s health report |

**Streaming.** `TrackStream` decodes into a 12 MB PSRAM ring, about 71 s at 44.1 kHz. It keeps about 35 % behind the playhead for cue and jog, and the rest ahead. A seek outside the window restarts the window there. Sources sit behind one interface:
- WAV: RIFF walk, including `WAVE_FORMAT_EXTENSIBLE`.
- MP3: [minimp3](main/third_party/minimp3) with `MP3D_SEEK_TO_SAMPLE`. The file is indexed at load (about 0.6 s for a 2:15 track at 256 kbps), so seeks land on exact samples.

**Waveform.** While decoding, the loader also records one peak byte per 128 frames, indexed by absolute frame (`TrackStream::peak`). The UI draws a playhead-centred view from that array into a 480×100 RGB565 canvas in PSRAM. One pixel is 512 frames, so the screen spans about 5.6 s. The audio itself is never read for drawing.

## Audio health: what the report means

Every 2 s while playing, the `report` task prints a block like this:

```
deck:   audio: realtime 100.0 %  block work avg 0.43 / max 0.70 ms (budget 5.80)  min slack 4.6 ms  clicks 0 (worst 0)
deck:   cpu: core0 30 %  core1 8 %  (waveform 47.3 fps)
deck:   top: IDLE1/1 92%  IDLE0/0 70%  taskLVGL/0 22%  audio/1 8%  loader/0 7%  ...
```

- **realtime:** frames handed to the DAC, divided by the frames the DAC must have played in the same wall time. **This is the dropout detector.** Anything under 100 % means gaps of silence. The I2S driver's "send queue overflow" event does *not* catch this: it stayed at zero while playback ran at 80 %.
- **block work:** time the audio task spends computing a block, excluding the I2S write. The budget is 5.8 ms.
- **min slack:** the shortest wait for DMA room. Close to 0 means the DAC is about to starve.
- **clicks:** spikes in the second difference of the DAC samples. **Exact on `sine.wav`.** On music, drum hits set it off, so treat it as a hint there.
- **cpu / top:** per-core load and the top tasks by CPU, from FreeRTOS run-time stats.

Rules that came out of using these:
- **Initialise drivers from the task that owns them.** An ESP-IDF driver's interrupt is allocated on the core that installs it. With `audio_out::begin()` called from `app_main` (core 0), the I2S interrupt shared core 0 with the screen's SPI traffic, and playback dropped to about 80 % real time whenever the waveform scrolled. The audio task now calls it, so the interrupt is on core 1.
- **Never log from the audio loop.** Two log lines over 115200 baud take longer than a block. The audio task fills a `Report` struct and the low-priority `report` task prints it.

The full story is on the blog: [The waveform was stealing my audio](https://audiodestrukt.com/blog/posts/esp32-deck-waveform-starved-the-dac.html).

## Screen notes

**SPI at 40 MHz.** About 47 fps for the waveform, against 19 fps at 20 MHz. If the image corrupts on longer wires, drop `kSpiHz` in `display.cpp`.

**LVGL flush wait.** By default LVGL waits for a finished SPI transfer with `while (flushing);`, which kept core 0 near 100 %. `display.cpp` sets a flush-wait callback instead: LVGL sleeps on a semaphore, and the transfer-done interrupt wakes it.

**Touch interrupt disabled.** On jumper wires, the 40 MHz SPI clock couples into the CTP_INT wire. Each false edge woke LVGL through `xEventGroupSetBitsFromISR`, which runs in the FreeRTOS timer task. That cost about 25 % of a core while the waveform scrolled. LVGL polls touch every ~30 ms anyway, so the interrupt isn't needed.

| Playing, waveform up | core 0 | core 1 | waveform |
|---|---|---|---|
| Busy-wait flush, touch INT on, 20 MHz | 98 % | 42 % | 19 fps |
| All three fixed, 40 MHz | 30 % | 8 % | 47 fps |

### Vsync

The ST7796S has a **TE** (tearing effect) output that pulses once per panel refresh, which is effectively vsync. **The MSP4021's 14-pin header doesn't bring TE out**, so this module has no vsync. The panel refreshes itself at about 60 Hz from its own memory, and SPI writes land whenever they arrive. A scrolling waveform can therefore show a tear line.

Options on this module:
1. **Scanline read (software vsync).** The ST7796S reports the row it is scanning (command `0x45`). Wire **SDO (MISO)** and start each waveform write just behind the scan, so a write never crosses it.
2. **Pace the frame rate.** Lock the waveform to 60 or 30 fps so any tear stays in a consistent place. Cheaper, but less effective.

Neither is done yet. The MSP4021 is a stand-in. The intended MIPI-DSI screen is driven with real video timing, and ESP-IDF's DPI panel driver provides vsync callbacks, which fixes tearing properly.

## Status and next

Working:
- WAV/MP3 streaming, varispeed, and keylock (including 48 kHz files).
- CUE, jog, the touch UI, and the scrolling waveform.
- Audio at 100 % real time with the waveform at ~47 fps.

Next:
- Pro DJ Link over the on-board Ethernet. Network work goes on core 0.
- Deck B and the crossfader.
- An overview waveform, with an analysis cache on the SD card.
- A stereo DAC (PCM5102A) and timecode input (PCM1808).
- The DSI screen.
