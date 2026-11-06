# freedj-esp

(in `p4/deck`; it started as "the P4 deck", then "ESP Deck")

A small CDJ-style deck on the **Waveshare ESP32-P4-Module-DEV-KIT**. It plays WAV and MP3 from a rekordbox USB stick or the SD card. You control it with a Numark DJ2Go or a Pioneer XDJ-1000MK2 over USB, the AS5600 jog, the touch screen, or serial keys. It has varispeed and keylock, CDJ-style CUE, a scrolling waveform, and beat sync to other players over Pro DJ Link.

This lives next to the A1S firmware in the repo root and doesn't replace it. Both builds share the deck event types, the DJ2Go decoder, WSOLA and the timecode decoder, all from `../../src`. Timecode vinyl works on both: on the A1S through its codec's line in, on the P4 through a USB audio interface (see *Timecode vinyl*).

## Hardware

| Part | Notes |
|---|---|
| Board | Waveshare ESP32-P4-Module-DEV-KIT. ESP32-P4 rev 1.3 (needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`), 2× RISC-V 360 MHz, 32 MB PSRAM, 16 MB flash |
| Audio out | On-board ES8311 codec (I2C 0x18) → NS4150B amp → speaker. Mono. Amp enable is GPIO 53 |
| SD | On-board slot, 4-bit SDMMC, 40 MHz, powered from internal LDO channel 4 |
| USB stick | rekordbox export, FAT32. Can be plugged in or pulled while the deck runs. Its tracks are read in the background (a notice shows progress). Pulled mid-track, the deck goes into an emergency loop |
| USB ports | Jumper H3 (between the right-hand USB pair and the Ethernet jack) picks between two paths. **Labelled DEVICE** = the on-board CH334F 4-port hub, so three ports work but the fourth (the direct one) is dead. **HOST, or no jumper** = only the direct port works. Use DEVICE, with the DJ2Go and the stick in two of the hub ports. ESP-IDF's USB host has no transaction translator, so the firmware limits the root port to full speed (12 Mbit/s) to reach a full-speed device like the DJ2Go behind the hub. The stick then runs at full speed too |
| Ethernet | On-board IP101 PHY (RMII). DHCP, or 169.254.x.x link-local on a cable straight to a player |
| Controller | Numark DJ2Go, or a Pioneer XDJ-1000MK2 in USB-MIDI control mode, in any of the four USB-A ports (USB host, class-compliant MIDI; Pioneer's vendor ID picks the XDJ map) |
| Screen (stand-in) | LCDwiki **MSP4021**: 4" 480×320, ST7796S over SPI + FT6336U capacitive touch. Or a 2.8" **MSP2834** (320×240 ILI9341V, same wiring; see *Build and flash*). Will be replaced by a MIPI-DSI screen |
| USB audio interface | Optional. A class-compliant UAC1 interface with 16-bit stereo, e.g. a Behringer UCA202 (TI PCM2902). Its output takes over from the speaker; its line in reads timecode vinyl. Tested: PCM2902 at 44.1 kHz both ways. UAC2-only interfaces don't work: the root port runs at full speed |
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

**2.8" screen** (LCDwiki MSP2834 / Hosyond 2.8" IPS, ILI9341V 320×240, same 14-pin wiring
as the MSP4021): build into its own directory with the screen's defaults layered on top:

```
docker run --rm -v $PWD:/repo -w /repo/p4/deck espressif/idf:release-v5.5 idf.py -B build28 \
  -D SDKCONFIG=build28/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.screen28" build
cd p4/deck/build28 && esptool --chip esp32p4 --port /dev/ttyACM0 -b 921600 write-flash @flash_args
```

To try the 2.8" layout without the 2.8" panel, build into `build28p/` with
`sdkconfig.defaults.preview28` added after the other two defaults files
(`CONFIG_FREEDJ_SCREEN_PREVIEW`). This runs the 4" panel as usual and gives the UI only its
top-left 320×240. The pixel pitches are close (0.174 / 0.180 mm), so it shows the layout at
almost its real size, touch included.

The choice is `menuconfig` > *freedj deck* > *Screen* (`CONFIG_FREEDJ_SCREEN_28`). The panels
have about the same pixel density (5.75 and 5.56 px/mm), so the 2.8" layout keeps the same
bands at the same physical size with smaller fonts: Montserrat 32 for the time, 12 for most
labels, 10 on the touch keys; the list shows about 3½ rows and the waveform 3.7 s. The
PERFORM pads are 6 × 4 instead of 8 × 3 (about 7 mm wide) and cover the time row while they're
open. Not tried on the panel yet: the landscape rotation flags and colour inversion are
LCDwiki's, and may need flipping.

## Controls

**DJ2Go (deck A):**
- **Browse knob:** move through the track list.
- **LOAD A:** load the selected track.
- **PLAY:** play or pause.
- **CUE:** CDJ-style.
  - While playing: jump back to the cue point and pause.
  - While paused: set the cue point here, play while held, return on release.
- **Pitch fader:** ±8 %.
- **Jog:** nudge while playing; **scratch while paused** (the audio follows the wheel, both directions). See *Feel parameters*.
- **SYNC:** beat sync to the Pro DJ Link tempo master on/off. Needs a track with a rekordbox beat grid.
- **Channel volume:** level.

**Pioneer XDJ-1000MK2 as a controller (deck A):** plug the XDJ's USB-B port into a P4 USB-A port. On the XDJ, choose **CONTROL MODE → USB MIDI**, then push the rotary selector when it says "push to connect". Re-select it after the P4 restarts. The map is the manual's *List of MIDI Messages* (p. 37), in `../../src/xdj_midi.cpp`:
- **Rotary selector:** browse; **push** to load; **BACK** goes up a level.
- **PLAY/PAUSE, CUE:** as on the DJ2Go.
- **Tempo slider:** pitch, with the + side faster.
- **Jog:** the top (touched) and the rim both send a speed. It's turned into jog ticks, so they nudge while playing and scratch while paused.
- **Hot cues A–H, CALL ◀ ▶, MEMORY, DELETE, LOOP IN/OUT, RELOOP, beat loops 1/2–16, beat jump ±1–32, QUANTIZE, SLIP, SYNC, TEMPO RANGE, MASTER TEMPO** (keylock) and **DIRECTION REV** all go to the same deck functions as the serial keys.
- **Sound plays from both.** The P4's speaker/line out always plays. The XDJ is also a USB sound card, so it gets a copy and plays the deck from its own outputs too, like a laptop running DJ software.
- **Not mapped yet:** SEARCH ◀◀ ▶▶, TRACK SEARCH, TIME MODE, JOG MODE, MASTER, the needle strip, and hot cue CALL/DELETE.

**Touch:**
- Tap a row to select it; tap it again, or press LOAD, to load it.
- Tap the waveform to bring the list back.
- Tap the **SYNC** and **KEY** chips in the header for sync and keylock.
- Touch or drag the **overview** strip under the time to jump there (needle search).
- Tap the **phase meter** to switch between the beat display and the alignment view.
- The **waveform stays up while paused**; the list comes back for a few seconds while browsing.
- **Bottom row: the XDJ's touch keys**, BROWSE · TAG LIST · INFO · MENU · PERFORM. TAG LIST, INFO and MENU are drawn dimmed: not built yet.
  - **BROWSE** shows the track list until pressed again or a track loads (then back to the waveform, as on the XDJ).
  - **PERFORM** opens the performance pads over the meter and waveform:
  - row 1: hot cues A–H (tap to set or jump, long-press to delete; momentary in slip)
  - row 2: CALL ◀ ▶ through the memory cues, MEM / DEL memory cue, loop /2 and x2, RELOOP/EXIT, SLIP
  - row 3: beat loops 1–16, beat jump ◀4 4▶, REV
- Tap the **range** label next to the tempo to cycle ±6/10/16/WIDE; long-press it for tempo reset.
- Tap the **Q** chip to toggle quantize.
- Tap the **timecode chip** (ABS / REL / INT / THRU, only with a USB audio input) to cycle the vinyl mode.
- **Long-press SYNC** to become tempo master. The BPM line shows MASTER in orange; other players can then sync to the deck.

**DJ2Go pitch bend − / +:** − toggles key lock, + switches the beat meter between bar boxes and phase ticks (as tapping it does). The DJ2Go no longer sets manual loops; use the touch screen or serial `i` / `u`.

**Serial keys** (console UART), for hands-off testing:

| Key | Action |
|---|---|
| `1`–`9` | select track |
| `l` | load |
| `p` | play/pause |
| `c` | cue (press + release) |
| `k` | keylock |
| `s` | sync |
| `[` `]` | trim our beat phase against the master, -/+ 2 ms |
| `g` | give the loaded track a 120 BPM test grid (SYNC without rekordbox) |
| `v` | switch the phase meter view |
| `n` | needle search to the middle of the track |
| `!` `@` `#` `$` `%` `^` `&` `*` | hot cue A–H (set / jump); `D` deletes A |
| `o` `x` `(` `)` | 4-beat loop, reloop/exit, loop 1/2, x2 |
| `i` `u` | loop in / out |
| `,` `.` | beat jump -4 / +4 |
| `<` `>` | call previous / next memory cue |
| `q` | quantize |
| `t` / `T` | tempo range / tempo reset |
| `m` / `M` | memory cue add / delete |
| `z` / `r` | slip / reverse |
| `Y` | become tempo master |
| `J` | jog calibration: ticks since the last `J` (see *Feel parameters*) |
| `P` | panel: CUE/PLAY state and presses, pitch pot raw/filtered/range/position (GPIO 24/25/22) |
| `V` | timecode vinyl mode: ABS → REL → INT → THRU (as tapping the chip) |
| `A` | timecode vinyl: absolute (needle drops seek) / relative |
| `f` | cycle the timecode decoder's flags (record plays backwards, or L/R swapped in the cabling) |
| `L` | timecode vinyl status (also in the 2 s report) |
| `Z` | timecode vinyl: the needle is on the record's first groove (sets track 0:00) |
| `w` | cycle the waveform test mode: normal → drawn but not sent to the screen → off |

[`tools/p4_serial.py`](../../tools/p4_serial.py) holds the port open without resetting the board, logs to a file, and sends anything appended to `<log>.cmd`. A whole experiment can be scripted, for example:

```
tools/p4_serial.py deck.log &
echo -n 3l >> deck.log.cmd; sleep 3; echo -n p >> deck.log.cmd
```

## Timecode vinyl

Plug a USB audio interface (*Hardware*) into a hub port and a turntable with a Serato Control Vinyl record into its line in, through a phono preamp or a mixer's line out. Load a track and press PLAY. Once the deck has heard 0.3 s of steady timecode at 0.6–1.5× (33 or 45), the record drives it:

- **Speed and direction** follow the record. Stopping it stops the audio; backwards plays backwards. Keylock hands over to varispeed below 0.5× and in reverse. The tempo fader, nudge and SYNC don't apply.
- **Jog:** with the record stopped, the jog scratches (as when paused); once the record moves it has the deck again.
- **Modes**, Serato's: tap the timecode chip (or serial `V`) to cycle **ABS → REL → INT → THRU**. INT ignores the record: the deck plays on at the record's last speed until the pitch fader moves (like SYNC off), with fader, jog, nudge, SYNC and keylock as normal.
- **THRU** plays a real record: the interface's input goes straight to the output (speaker and USB), through the deck's volume. The deck is bypassed and holds its place (nothing is mixed, so the track pauses), and the decoder stops. Swap records, switch modes. It adds ~12 ms: 512 frames of input stay queued for the interface's 4 ms bursts, and since its clock isn't ours, a block now and then reads one frame more or less (0.4 %, stretched over the block). Back to ABS, the first needle-down re-syncs.
- **Absolute mode** (ABS, default; `A` toggles ABS / REL): the track follows the record's position too, with 0:00 at the record's first groove. Lift the needle and drop it further in, and the track jumps there. Inside a loop the loop wins.
  - **Needle drops:** after a lift, the deck stays silent until the decoder has a position (bit lock), then jumps there: about 0.1–0.5 s, typically 0.2 s, plus any MP3 decode at the new spot. No lock within 1 s: it follows speed only.
  - **Staying on it:** a jump of 300 ms or more seeks at once; a smaller steady error (after a scratch, or a seek that waited for the decoder) re-syncs after 0.2 s, once that audio is decoded. Steady play stays within ~25 ms.
  - **Record start:** our Serato Control Vinyl's first groove reads 24.0 s of timecode, so that's track 0:00 (`kStartMs` in `vinyl.cpp`). Another record: drop on the first groove and press `Z`. Before the start, the track waits at 0:00.
- **Relative mode:** speed and direction only.
- **The timecode chip**, left of Q, shows the mode and only appears while a USB audio input streams (the track name gives it room; without an interface the header is unchanged). Green: the record drives and moves. Orange: drives, stopped. Outline: waiting for timecode. Grey: INT. Blue: THRU. The PLAYING / PAUSED chip is unchanged.
- An interface used only for its output, or music on its line in, never takes over: it needs the timecode carrier first.

The decoder (`../../src/timecode.cpp`) is the A1S one, a clean-room version of xwax's. The P4 side is `main/vinyl.cpp` (decoder task, core 0) and the input half of `main/usb_audio.cpp`. A 45 rpm record plays 1.35× fast: there's no 33/45 setting yet.

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

**Loader pacing.** After a load or a jump, the loader decodes at full speed until about 8 s are buffered ahead, then paces the rest of the window so the screen keeps its frame rate.

**Streaming.** `TrackStream` decodes into a 12 MB PSRAM ring, about 71 s at 44.1 kHz. It keeps about 35 % behind the playhead for cue and jog, and the rest ahead. A seek outside the window restarts the window there. Sources sit behind one interface:
- WAV: RIFF walk, including `WAVE_FORMAT_EXTENSIBLE`.
- MP3: [minimp3](main/third_party/minimp3) with `MP3D_SEEK_TO_SAMPLE`. The file is indexed at load (about 0.5–0.6 s per track), so seeks land on exact samples and a needle jump leaves only a ~46 ms gap. The index is built even when the MP3 has a VBR tag; otherwise minimp3 would build it during the first far seek, mid-track.

**Boot.** The bootloader turns the screen's backlight off about 50 ms after power-on (`bootloader_components/backlight_off`); the module's backlight is otherwise on, showing blank, white panel memory. The app draws a splash ("FREEDJ-ESP", build date, a status line) as its first step, at about 1.0 s, and the deck screen replaces it at about 3.1 s. The 32 MB PSRAM self-test is off (`CONFIG_SPIRAM_MEMTEST=n`); it cost about 1 s per boot.

**Colour waveform.** For rekordbox tracks, the scrolling waveform and the overview use rekordbox's colour detail waveform (`PWV5` in the `.EXT`: 150 columns per second, RGB 3 bits each plus a 5-bit height). The bit order was checked against the monochrome `PWV3` heights. Other tracks use the blue waveform below.

**Waveform.** While decoding, the loader also records one peak byte per 128 frames, indexed by absolute frame (`TrackStream::peak`). The UI draws a playhead-centred view from that array into a 480×100 RGB565 canvas in PSRAM. One pixel is 512 frames, so the screen spans about 5.6 s. The audio itself is never read for drawing.

## Audio health: what the report means

Every 2 s while playing, the `report` task prints a block like this:

```
deck:   audio: realtime 100.0 %  block work avg 0.49 / max 0.99 ms (budget 5.80)  min slack 4.3 ms  clicks 0 (worst 0)
deck:   cpu: core0 42 %  core1 9 %  (screen 41.4 fps, waveform drawn 75/s)
deck:   mem: internal 93 KB free (lowest 73 KB, largest block 38 KB), PSRAM 19394 KB free
deck:   top: IDLE1/1 91%  IDLE0/0 58%  taskLVGL/0 29%  audio/1 9%  loader/0 8%  link/0 2%  ...
deck:   link: player 2  192.168.68.58  peers 1  master 5 @ 128.00 BPM
deck:   sync: phase error avg 1.4 / max 3.3 ms (offset +0 ms)
```

- **realtime:** frames handed to the DAC, divided by the frames the DAC must have played in the same wall time. **This is the dropout detector.** Anything under 100 % means gaps of silence. The I2S driver's "send queue overflow" event does *not* catch this: it stayed at zero while playback ran at 80 %.
- **block work:** time the audio task spends computing a block, excluding the I2S write. The budget is 5.8 ms.
- **min slack:** the shortest wait for DMA room. Close to 0 means the DAC is about to starve.
- **clicks:** spikes in the second difference of the DAC samples. **Exact on `sine.wav`.** On music, drum hits set it off, so treat it as a hint there.
- **cpu / top:** per-core load and the top tasks by CPU, from FreeRTOS run-time stats.
- **screen fps:** frames LVGL actually rendered and sent to the panel. "Waveform drawn" counts redraws of the waveform canvas, which can be higher, because LVGL folds several into one frame. Before 2026-10-31 the report only had the redraw count, so older fps figures read high.
- **mem:** internal RAM free now, lowest since boot, and largest free block. Internal RAM is the tightest resource: task stacks and DMA buffers must come from it.
- **link / sync:** our address and peers, who the tempo master is, and while synced, the phase error against the master.

Rules that came out of using these:
- **Initialise drivers from the task that owns them.** An ESP-IDF driver's interrupt is allocated on the core that installs it. With `audio_out::begin()` called from `app_main` (core 0), the I2S interrupt shared core 0 with the screen's SPI traffic, and playback dropped to about 80 % real time whenever the waveform scrolled. The audio task now calls it, so the interrupt is on core 1.
- **Never log from the audio loop.** Two log lines over 115200 baud take longer than a block. The audio task fills a `Report` struct and the low-priority `report` task prints it.

The full story is on the blog: [The waveform was stealing my audio](https://audiodestrukt.com/blog/posts/esp32-deck-waveform-starved-the-dac.html).

## Screen notes

**SPI at 40 MHz.** Roughly twice the frame rate of 20 MHz. If the image corrupts on longer wires, drop `kSpiHz` in `display.cpp`.

**LVGL flush wait.** By default LVGL waits for a finished SPI transfer with `while (flushing);`, which kept core 0 near 100 %. `display.cpp` sets a flush-wait callback instead: LVGL sleeps on a semaphore, and the transfer-done interrupt wakes it.

**Touch interrupt disabled.** On jumper wires, the 40 MHz SPI clock couples into the CTP_INT wire. Each false edge woke LVGL through `xEventGroupSetBitsFromISR`, which runs in the FreeRTOS timer task. That cost about 25 % of a core while the waveform scrolled. LVGL polls touch every ~30 ms anyway, so the interrupt isn't needed.

| Playing, waveform up | core 0 | core 1 | waveform |
|---|---|---|---|
| Busy-wait flush, touch INT on, 20 MHz | 98 % | 42 % | 19 redraws/s |
| All three fixed, 40 MHz | 30 % | 8 % | 47 redraws/s |

(Measured with the old counter, which counted waveform redraws rather than frames sent; see the changelog.)

### Vsync

The ST7796S has a **TE** (tearing effect) output that pulses once per panel refresh, which is effectively vsync. **The MSP4021's 14-pin header doesn't bring TE out**, so this module has no vsync. The panel refreshes itself at about 60 Hz from its own memory, and SPI writes land whenever they arrive. A scrolling waveform can therefore show a tear line.

Options on this module:
1. **Scanline read (software vsync).** The ST7796S reports the row it is scanning (command `0x45`). Wire **SDO (MISO)** and start each waveform write just behind the scan, so a write never crosses it.
2. **Pace the frame rate.** Lock the waveform to 60 or 30 fps so any tear stays in a consistent place. Cheaper, but less effective.

Neither is done yet. The MSP4021 is a stand-in. The intended MIPI-DSI screen is driven with real video timing, and ESP-IDF's DPI panel driver provides vsync callbacks, which fixes tearing properly.

## Feel parameters

How the deck responds to your hands. All in one block at the top of `main/main.cpp` ("Feel"), except the two screen ones, which are in `main/ui.cpp`. These are first guesses, tuned on the DJ2Go. Refine them by comparing with other hardware, and note changes in the changelog.

| Parameter | Value | What it does | Compare against |
|---|---|---|---|
| `kScrubFrames` | 441 (10 ms per tick) | How far one jog tick moves the track when paused (scratch). Together with the jog's ticks per revolution, this sets **track per revolution**. | A 33⅓ rpm record moves about **1.8 s per revolution**, and Pioneer's vinyl mode is modelled on that. To match it: measure ticks per revolution (below), then set `kScrubFrames = 1.8 × 44100 / ticks`. |
| `kScratchChase` | 1024 frames (23 ms) | How quickly the playhead catches up with the jog. Lower is tighter but rougher; higher is smoother but laggier. | The lag between hand and sound on a CDJ in vinyl mode. Turntables have none, so lower is more vinyl-like if it stays smooth. |
| `kScratchMaxSpeed` | 8× | Fastest scratch speed, either direction. | Fast scratches past ~4× sound like noise on most decks. Mostly a safety cap. |
| `kNudgePerTick` | 0.4 % per tick | Pitch bend per jog tick while playing. | Nudging a CDJ's jog edge. Too high and alignment overshoots; too low and you need lots of wheel. |
| `kNudgeMax` | ±30 % | The most nudges can add up to. | |
| `kNudgeDecay` | 0.97 per block | How fast a nudge fades once you stop turning (per 5.8 ms block; about 50 % in 130 ms). | How long a CDJ's speed takes to settle after a nudge. |
| `kRanges` | ±6 / ±10 / ±16 % / WIDE | Pitch fader ranges. Tap the range label to cycle. | The XDJ-1000's ranges (WIDE = ±100 %). |
| `kSyncGain` | 0.3 | How much of the phase error SYNC corrects. | Not a hand feel, but it decides how sync "feels": higher locks faster but can wobble. |
| `kSyncMaxBend` | 3 % | The most SYNC bends the tempo while correcting. | Audible as a slight pitch bend on big corrections; CDJs bend similarly. |
| `kVinylSmooth` | 0.35 per block (~15 ms) | Smoothing on the record's speed. Lower is steadier but lags a hand on the platter. | A turntable through Serato: no audible lag on a scratch. |
| `kVinylKeylockMin` | 0.5× | Below this, keylock gives way to varispeed (WSOLA smears near 0). | |
| `kVinylSeekMs` / `kVinylSeekGapMs` | 300 ms / 250 ms | Absolute mode: how far off the record the track must be before it jumps (a needle drop), and how often it may. | Same as the A1S. |
| `kVinylTrimMs` / `kVinylTrimHoldMs` | 25 ms / 200 ms | Smaller errors re-sync once they've held this long (only when the audio there is decoded). | |
| `kVinylLeadMs` | 60 ms | The playhead runs this far ahead of the record so the sound (I2S + USB out + USB in delay) lines up with it. | Estimated, not measured: a click on the record vs the sound would pin it. |
| `kThruFill` / `kThruSlack` | 512 / 64 frames | THRU's input queue and how far its average may stray before a block reads a frame more or less. | Lower fill = less delay but more dropouts (10 in ~9 min at 512, with one USB error, clustered); slack trades trims for drift. |
| `kLiftMs` / `kSettleMaxMs` (vinyl.cpp) | 150 ms / 1 s | A carrier gap this long is a lift; then hold silent until bit lock, at most this long. | Serato: a needle drop lands on the new spot without scratching the old one. |
| `kMinCutoff` / `kDeadband` (as5600.cpp) | 30 Hz / 1 tick | The AS5600 jog's smoothing at rest and the ticks needed to start or reverse. 1 Hz / 3 ticks hid ±100 counts of noise but lagged (first tick 88 ms after a start); the sensor is clean now (0 counts at rest). | Back to 1 Hz / 3 if the noise returns: run `R` at rest. |
| `kListLingerMs` (ui.cpp) | 3 s | How long the track list stays after browsing, before the waveform returns. | |
| `kBinsPerPx` (ui.cpp) | 4 (≈5.6 s across) | Waveform zoom. | The XDJ's default zoom. |

**Measuring the jog (serial `J`).** Press `J` to zero the tick counter, turn the jog exactly one revolution, then press `J` again. The log shows the ticks for that revolution and how much track they moved at the current setting:

```
deck: jog: 128 ticks since last J; at 441 frames/tick that's 1.28 s of track
```

Do the same on other controllers (in their own software) to compare, and set `kScrubFrames` for the track-per-revolution you want.

The DJ2Go's jog has no touch sensor, so the deck can't tell a hand on the platter from a nudge. Paused, the jog scratches; playing, it nudges. True vinyl mode, scratching mid-play, needs a touch-sensitive jog, so it waits for the I2C control panel.

## Resource budget

The current numbers. The changelog ([CHANGELOG.md](CHANGELOG.md)) has the budget at every step. Update both when a change moves them.

Measured 2026-10-31: `house.mp3` from SD, synced to a Pro DJ Link master, with the waveform, beat grid and phase meter up.

| | Varispeed | Keylock | Headroom |
|---|---|---|---|
| Core 0 (UI, loader, USB, network) | 39–42 % | 38–42 % | ~58 % |
| Core 1 (audio only) | 9 % | 27–28 % | ~72 % |
| Worst audio block (budget 5.8 ms) | 1.25 ms | 3.4 ms | |
| Screen | 40–42 fps | 40–42 fps | |

- **Core 0 by task:** LVGL 25–29 %, loader 8 %, Link 2 %. The waveform plus meter cost about 22 points; core 0 is at 20 % with the waveform off.
- **Internal RAM:** 102–106 KB free while playing a stick track, 80 KB lowest. The list strings are in PSRAM since 2026-10-31. **This is the tightest resource.**
- **With a 224-track rekordbox stick, playing MP3s from it:** core 0 49–50 % (LVGL 34 %, about 8 points more than with the SD list; the suspect is the long list), screen 37–38 fps. Internal RAM 65 KB free, **43 KB lowest**. USB transfer buffers are in PSRAM: from internal RAM, a 32 KB stick read failed and crashed the driver.
- **PSRAM:** 19.4 MB free (12 MB of it is the track window, which could shrink).
- **App flash:** 1.4 MB of an 8 MB partition (`partitions.csv`, 83 % free); about 8 MB of the 16 MB flash is unused.

## Status and next

The working list for the next session is in [TODO.md](TODO.md). What changed when: [CHANGELOG.md](CHANGELOG.md). Other chips and boards it could run on: [TARGETS.md](TARGETS.md). The 3D-printed enclosure: [hardware/](hardware/README.md).

## Pro DJ Link and SYNC

- The deck sends beat packets on its beats, can take tempo master (with the
  proper handoff, both ways), and accepts SYNC on/off from other players.
  Mixer on-air and fader start are implemented but untested.

- `prolink.cpp` announces the deck as player 2 and sends status to the other players. It listens to everyone's beats and status to find the tempo master. Packet layouts come from freedj-3000, which checked them against an XDJ-1000MK2.
- `rekordbox.cpp` reads each track's `PIONEER/USBANLZ/…/ANLZ0000.DAT`. The `PPTH` section gives the audio file and `PQTZ` the beat grid, so `export.pdb` isn't needed.
- SYNC matches the master's tempo (taking half or double time when that's closer) and snaps onto its beat at PLAY. It then holds phase by bending the tempo by up to 3 %. Phase is measured at the speaker: the ~35 ms of queued DMA audio is subtracted, plus the time-stretcher's own delay in keylock.
- Measured against prolink-cpp's virtual CDJ: phase error 1.4 ms average (3 ms worst) in varispeed, about 3 ms (around 10 ms worst) in keylock. Not yet checked by ear against a real player.

Working:
- WAV/MP3 streaming, varispeed, and keylock (including 48 kHz files).
- CUE, jog, the touch UI, and the scrolling waveform.
- Audio at 100 % real time with the waveform, beat grid and phase meter up (screen ~41 fps).
- XDJ-style phase meter (beat display / alignment view) and beat grid on the waveform.

- Pro DJ Link beat sync, with rekordbox beat grids from a USB stick.

Next:
- Test against a real XDJ, and send beat packets so the deck can be tempo master.
- Deck B and the crossfader.
- An overview waveform, with an analysis cache on the SD card.
- A stereo DAC (PCM5102A). Timecode input is done, over a USB audio interface.
- The DSI screen.
