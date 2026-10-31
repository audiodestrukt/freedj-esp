# P4 deck changelog

Newest first. Each entry lists what changed, then the resource budget measured
at that point, so the cost of every feature stays on record. The current
budget is also in [README.md](README.md#resource-budget).

How the numbers are measured: the deck prints a report every 2 s while
playing (see the README's *Audio health* section). Unless noted, it's
`house.mp3` (256 kbps, 2:15) from the SD card, playing with the waveform up.
**Frame rates before 2026-10-31 counted waveform redraws, not frames sent to
the panel**, so they read high. From 2026-10-31 on, "screen fps" counts
frames LVGL actually rendered and flushed.

## 2026-10-31: printed jog shaft, AS5600 encoder, M3 inserts on the base (CAD only)

- The jog shaft is printed in one piece with the platter (Ø7.95, snug in the
  608s), instead of a steel 8 mm shaft. `fit_gauge.stl` has four pins
  (7.85-8.00) to find the right diameter on your printer first.
- The encoder is the AS5600 breakout (WWZMDiB, 23 x 23 mm), not an MT6701.
  It lies flat on the base plate on two small M3 heat-set inserts. Its
  right-angle header points back, and the solder tails drop into a slot.
- The stack is worked out from the shell as printed (lower bearing at
  z 10). From there down: DIN 6799 size-6 e-clip groove (9.20-9.95), then
  0.4 mm of plastic, then a pocket with the 1.6 mm magnet glued flush. The
  shaft tip is at z 7.20, 1.1 mm above the chip. Shaft below the platter:
  29.3 mm flat, 25.3 mm angled. The shell is unchanged.
- The dev board standoffs take small M3 inserts (Ø4.0 bore, 4.5 deep, 8 mm
  OD) instead of M2.5. The board's 2.75 mm holes need opening to 3.2 mm.
- Fix: `base.py` always wrote the angled plate as `base.stl`. The flat
  config now writes `base_flat.stl`, 1.6 mm deeper. The platter is also
  per config now (`platter_flat.stl`).
- No firmware change, so the budget is unchanged.

## 2026-10-31: centred screen window, base screws in all four corners (CAD only)

- The screen window is centred on the deck (over the jog). Before, it sat
  about 7 mm right: the module was moved 4 mm for the USB slot, and the
  active area is 2.88 mm off-centre on top of that. Now the module shifts
  2.88 mm left instead; the USB slot and engraving moved left to suit.
- Base fixing redone: four short corner bosses (9 mm, 10 mm tall, 45° cone
  on top) instead of full-height posts. The old front-left post's insert
  bore broke into the PLAY button hole, and the back-right one had been
  moved off the corner. The dev board moved 5.5 mm left to clear the
  back-right boss; RCA moved left again (64/80 mm).
- Tempo-zero LED hole added (it was missing).
- No firmware change, so the budget is unchanged.

## 2026-10-31: dev board mounted in the enclosure (CAD only)

- The Waveshare ESP32-P4-Module-DEV-KIT (Pi B+ footprint, from Waveshare's
  drawing) sits back-right under the screen, on four 3 mm standoffs printed
  on the base plate (M2.5 heat-set inserts). Ethernet and both USB-A pairs
  come out the rear wall; power and UART USB-C come out the right side.
- The separate RJ45 and USB-C jacks are gone; RCA moved left of the ports.
  The back-right base post moved forward, between the pitch pot and the board.
- The flat version is now 38 mm tall (was 34): the USB-A stacks would hit
  the screen's board under a flat top. The angled version clears at 34.
- No firmware change, so the budget is unchanged.

## 2026-10-31: renamed freedj-esp

- The project (and repo) is now **freedj-esp**, after freedj-3000. The splash
  and the Pro DJ Link device name say FREEDJ-ESP, and the enclosure engraving
  too. CAD file names and the config variable follow (`freedj_esp*`,
  `FREEDJ_CONFIG`).
- Name strings only, so the budget is unchanged. Builds clean (P4, and the
  A1S es8388 env).

## 2026-10-31: screen recessed behind a window (CAD only)

- The screen now sits behind the top plate on a 0.5 mm foam gasket. The
  window shows only the visible area: it's off-centre by 2.88 mm, as the
  panel's active area is, and chamfered 45° so fingers reach the edges.
- The four screen bosses come down to the board (6.8 mm OD); the M3 pilots
  are 6 mm deep, 3.3 mm under the face.
- No firmware change, so the budget is unchanged.

## 2026-10-31: screen mounts on its own 4 holes (CAD only)

- The screen is a Hosyond 4" capacitive, i.e. the LCDwiki MSP4031: the model
  now follows that drawing (108.00 × 60.88 board, 4.85 mm glass + tape stack).
- Four bosses on the module's mounting holes (102.00 × 54.88 mm, Ø3.2), with
  M3 × 5 thread-formers, replace the two printed clamps.
- No firmware change, so the budget is unchanged.

## 2026-10-31: compacted enclosure (CAD only)

- 20 mm shorter: 160 × 185 mm angled, 186 mm flat (was 205). The glass now
  starts 8 mm behind the jog well (was about 20). Jog, buttons and slider
  moved 5 mm forward, and the brand engraving moved onto the bezel. The
  depth is now derived from the layout instead of set by hand.
- No firmware change, so the budget is unchanged.

## 2026-10-31: enclosure around the 4" MSP4021, plus a flat-top version (CAD only)

- Redesigned around the 4" MSP4021 we actually have, not a 3.5" panel: 160 ×
  205 mm (was 195). The glass stack sits flush in the top and the board is
  clamped from behind. Panel dimensions are from vendor data, not measured yet.
- A flat-top configuration (`machine_flat.py`) that prints with no supports.
  The angled design stays the default.
- No firmware change, so the budget is unchanged.

## 2026-10-31: enclosure design (CAD only)

- First design for a 3D-printed ESP Deck in `hardware/`: a 160 × 195 mm
  CDJ around a 3.5" ST7796S LCD. 100 mm jog on two 608 bearings with an
  MT6701 magnetic encoder, CUE/PLAY with light rings, 45 mm pitch slider,
  12° screen bezel. Parametric build123d model, Blender renders.
- No firmware change, so the budget is unchanged.

## 2026-10-31: 8 MB app partition

- Our own partition table (`partitions.csv`) gives the app 8 MB instead of
  the 1.5 MB "single app large". The app is 1.4 MB, so 83 % is free (it was
  94 % full). About 8 MB of flash stays unallocated, for OTA slots or a
  cache later.

## 2026-10-31: XDJ touch keys, boot splash, colour waveforms, "ESP Deck"

- **Bottom row is now the XDJ's touch keys:** BROWSE (SEARCH) · TAG LIST ·
  INFO (LINK INFO) · MENU (UTILITY) · PERFORM, in freedj-3000's colours.
  - TAG LIST, INFO and MENU are dimmed: not built yet.
  - BROWSE holds the list until pressed again or a track loads.
  - PERFORM replaces PADS.
  - The CUE / PLAY / LOAD buttons are gone: they're on the DJ2Go, and
    tapping the selected row still loads.
- **Boot:** the bootloader hook switches the backlight off about 50 ms after
  power-on (no more white screen). The splash is the app's first step, now
  at 1.0 s (the white screen used to last 3.7 s). The deck screen comes at
  3.1 s (was 3.8). The PSRAM self-test is off (−1 s), and touch now starts
  after audio, because it shares the I2C bus.
- **Colour waveforms** from rekordbox's PWV5, on the scrolling waveform and
  the overview. Bit order checked: red 15–13, green 12–10, blue 9–7, height
  6–2; that layout correlates 0.86 with the PWV3 heights, rekordcrate's 0.63.
  Same cost as the blue waveform: 37.8 fps, core 0 59 % while the buffer
  refills.
- **Named ESP Deck:** on the splash, and as the Pro DJ Link device name.
- App: 6 % of the partition left. The bigger partition is due.

## 2026-10-31: scratch when paused, waveform stays up, feel parameters

- **Paused jog scratches.** The jog moves a target, and the playhead chases
  it over about 23 ms. The audio follows the hand at whatever speed and
  direction that takes, and glides to a stop. Varispeed, capped at 8×.
  Playing, the jog still nudges.
- **The waveform stays on screen while paused.** The list comes back only
  for a few seconds after browsing.
- **Feel parameters** gathered in one block in `main.cpp`, with the nudge
  numbers named, and documented in the README with what to compare each one
  against. Serial `J` counts jog ticks per revolution for calibration.

## 2026-10-31: tempo master, beat packets, remote sync, mixer (XDJ parity step 2, first half)

- **Beat packets** go out on each of our beats as they're heard. BPM is
  unpitched with pitch separate (Beat Link's effective tempo), beat-in-bar
  comes from the grid, and the six countdowns are filled in. The link task
  holds the pending beat until it's sent; before that, a late wake-up skipped
  every other beat. A backwards move (loop wrap, cue, jump) restarts the
  count, so loops keep sending beats.
- **Tempo master:**
  - Long-press SYNC (or serial `Y`) asks for master. If nobody has it, the
    deck takes it at once.
  - Otherwise it sends a master request (0x26) and waits for the yield
    (0x27), then for the master to name us in its status before claiming.
    Claiming on the yield alone made an XDJ abort (freedj-3000's finding).
  - When we're master and someone asks, we yield, name them, and let go
    once they claim.
  - The sync counter is set to the largest seen + 1. Status goes out at once
    when the master flag changes. While we're master, we follow nobody.
- **Remote control:** another player can turn our SYNC on or off, or tell us
  to become master (0x2a).
- **Mixer** (from Deep Symmetry's analysis; **untested, no DJM here**):
  "channels on air" (0x03) sets our on-air flag (red time, status bit), and
  fader start (0x02) plays, or stops and returns to the cue.
- The BPM line shows MASTER in orange when we're master.

Measured with prolink-cpp tools on the PC:

| Test | Result |
|---|---|
| Beat packets, 33 beats at 126 BPM | mean interval 0.4762 s (exact), 5.3 ms jitter as received on the PC; beat-in-bar 1-2-3-4 correct, also through a 4-beat loop |
| Taking master from the virtual CDJ | it yielded, named us, we took master (sync counter 1) |
| A virtual CDJ in `--follow-master` | synced to us (120 → 126 BPM); its beats within about ±15 ms of ours |

Test trap: a `prolink_listener` on the PC shares port 50001 with the virtual
CDJ, and Linux delivers a unicast packet to only one of them. With the
listener running, our master requests never reached the virtual CDJ.

## 2026-10-31: tempo range and reset, hot cues E–H, memory cues, slip, reverse

- **Tempo range** ±6 / ±10 (new default, as on the XDJ; was ±8) / ±16 /
  WIDE (±100 %). Tap the range label next to the tempo to cycle; long-press
  it for **tempo reset**, which plays at 0 % regardless of the fader.
- **PADS has three rows:** all eight hot cues A–H; CALL ◀ ▶, MEM, DEL, /2,
  x2, RELOOP, SLIP; beat loops 1–16, beat jump ◀4 4▶, REV. The panel now
  covers the meter and the waveform.
- **Memory cues can be set on the deck:** MEM stores the cue point (or the
  active loop's start), DEL removes the one at the cue or the playhead. These
  last for the session only and aren't written to the stick.
- **Slip:** during a loop, reverse, or a held hot cue, a shadow playhead
  keeps going at normal speed. Letting go resumes from it, and a grey line on
  the waveform shows where it is. In slip mode, hot cues are momentary.
- **Reverse:** always plays varispeed, because the time-stretcher only runs
  forwards. Sync correction pauses while reversed.

Measured (synced to the virtual CDJ):

| Test | Result |
|---|---|
| Slip through a 6 s loop | resumed 6 s further on, sync 1.5 / 4.8 ms |
| Reverse 2 s with slip | resumed ahead; one 119 ms sync reading right after, then 1.4 ms |
| Audio | 100 % real time throughout |

## 2026-10-31: cues, loops, beat jump, quantize (XDJ parity step 1)

- **Hot cues A–H and memory cues read from rekordbox.** `PCO2` in the
  `.EXT` is preferred, falling back to `PCOB` in the `.DAT`. A track loads at
  its first memory cue, as on a CDJ.
- **Hot cue pad:**
  - empty: stores a cue at the (quantized) position, or the active loop
  - set: jumps there and plays, as a CDJ does; a hot loop starts its loop
  - long press: deletes it
- **Loops:**
  - beat loops of 1–16 beats from the current beat
  - manual in/out, on the DJ2Go's LOOP IN / LOOP OUT
  - RELOOP/EXIT, and halve/double (in beats for beat loops)
  - the emergency loop now shares this loop engine
- **Beat jump ±4.** It moves an active loop along with it. **CALL ◀ ▶** steps
  through the memory cues.
- **Quantize** (on by default, as on the XDJ): cue, hot cue and loop points
  snap to the beat. Jumps land at the same point within the beat as where
  you left, so the track keeps time and SYNC holds.
- **Screen:**
  - PADS panel over the waveform: A–D, CALL ◀ ▶, 1/2, x2, loops 1–16,
    RELOOP, ◀4 4▶
  - Q chip in the header
  - the Bars countdowns are live: bars.beats to the next memory cue and the
    next hot cue
  - waveform: loop band, hot cues green, memory cues red; overview ticks too

Measured synced to the virtual CDJ: phase error 1.4 / 3–4 ms through every
loop, halve, double, exit, hot cue jump, beat jump and manual loop.

Budget (stick track, synced, hot cue + 4-beat loop, waveform up, buffer
still filling):

| | |
|---|---|
| Screen | 34 fps |
| Core 0 | 60 % (LVGL 36 %, loader 18 % refilling, Link 3 %) |
| Core 1 | 12 % |
| Worst audio block | 1.5 ms |
| Internal RAM | 95 KB free, 79 KB lowest |

## 2026-10-31: boot without waiting for USB, hot-swap sticks, emergency loop

- **Boot no longer waits for the stick.** The screen is up at 3.8 s (was
  about 19 s) with the SD tracks. A mounted stick is read in the
  background, with a "Reading USB stick… n" notice, and its tracks join the
  list when done (13–14 s for 218 tracks at full speed).
- **Sticks can be pulled and plugged in while the deck runs.** The track list
  is now an immutable snapshot that gets swapped whole. Load requests carry
  paths instead of list indexes, and the selection follows its track across
  list changes. On a pull, the scan is cancelled, the stick's tracks leave
  the list, and the loader closes its file before the stick is unmounted.
- **Emergency loop**, as on a CDJ. If the stick is pulled while its track
  plays, the deck loops 4 beats (from the grid; 2 s without one) out of the
  audio already decoded in PSRAM. Whole beats keep SYNC locked: measured
  1.4 ms through the loop. Status goes out as play state `0x12`, the screen
  shows E.LOOP, and loading another track ends it. Pulled while paused,
  the track just unloads.
- **List strings moved to PSRAM.** Internal RAM at boot: 112 KB free (was
  68 KB).

Budget:

| | |
|---|---|
| Boot to screen | 3.8 s |
| Internal RAM, playing a stick track | 102–106 KB free, **80 KB lowest** (was 43 KB) |
| Emergency loop | audio 100 % real time, sync 1.4 / 3.4 ms |

## 2026-10-31: all USB ports (hub) working, DJ2Go and stick together

- **Hardware:** jumper H3 on its **DEVICE** side connects the on-board
  CH334F hub. The HOST side, or no jumper, gives only the single direct
  port. With the hub on, three ports work and the direct one is dead.
- **Firmware:** ESP-IDF's host has no transaction translator, so the DJ2Go
  (full speed) failed behind the high-speed hub ("TT is not supported").
  The root port is now limited to full speed with `HCFG.FSLSSupp`, which is
  re-asserted every 100 ms because a port recovery clears it. That makes the
  hub run at full speed. This is the trick from espp's `usb_host`
  (`full_speed_only`). The port is powered only after the MIDI and MSC
  clients have registered.
- **Cost:** the stick runs at 12 Mbit/s too.

| | High speed (direct port) | Full speed (hub) |
|---|---|---|
| Boot scan, 218 ANLZ files | 6.9 s | 12.4–13.7 s |
| MP3 load (index built at load, reads the whole file) | 0.49 s | 4.94 s |
| Playback | fine | fine (0 underruns) |

Next: build the MP3 index in the background after the track is ready, and
cache the track list on the stick.

## 2026-10-31: track overview with needle search

- The progress bar is replaced by an overview of the whole track, drawn from
  rekordbox's preview waveform (`PWAV`, 400 columns). The played part is
  dimmed; white line = playhead, orange = cue. Tracks without analysis (SD)
  get a plain strip.
- **Needle search:** touch or drag the overview to jump there. With SYNC on,
  the deck snaps back onto the master's beat once audio is there.
- After any stall while synced, the deck re-snaps. Before, it bent its way
  back over about 4 s.
- **MP3 index built at load.** With a VBR/Info tag (rekordbox exports have
  one), minimp3 built its seek index on the first far seek. That read 5 MB
  and left a 0.53 s gap mid-track. Now it's built at load (+0.49 s per load),
  and the jump gap is 46 ms.
- **Loader paced** once 8 s are buffered ahead. Refilling the 70 s window at
  full speed after a jump had held core 0 at 100 % and the screen at 9 fps
  for about 4 s.
- Tried FAT fast seek: no effect, so it's not enabled.

Budget (stick MP3, synced, steady state after a jump):

| | |
|---|---|
| Screen | 38 fps |
| Core 0 | 48 % (LVGL 34 %, loader 9 %, Link 2 %) |
| Core 1 | 11 % |
| Worst audio block | 1.5 ms |
| Sync | 1.4 / 4 ms |
| Internal RAM | 68 KB free, 43 KB lowest |

- **The overview** costs nothing measurable. It redraws only when the
  playhead moves a pixel.
- **During the paced refill** (about 10 s after a load or jump): core 0
  58 %, screen 34 fps.
- **Jump gap:** 46 ms.
- **App:** 9 % of the partition free.

## 2026-10-31 (fix): loading a stick track reset the board

- **Cause:** while minimp3 built its seek index, FAT read a large run of the
  file in one go. `usb_host_msc` then grew its DMA transfer buffer to about
  32 KB in internal RAM, but the largest free block was 31 KB. The
  allocation failed after the old buffer had been freed, and the driver
  freed it again on the error path, so the heap asserted
  (`tlsf_free … block already marked as free`) and the board reset.
- **Fix:** USB transfer buffers live in PSRAM
  (`CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM`), and the phase meter's
  tick canvas (15 KB) moved to PSRAM too.

Budget (three rekordbox MP3s from the stick, synced, waveform + grid +
meter up, 224-track list):

| | |
|---|---|
| Screen | 37–38 fps |
| Core 0 | 49–50 % (LVGL 34 %, loader 9 %, Link 3 %) |
| Core 1 | 10–11 % (varispeed) |
| Worst audio block | 1.5 ms |
| Sync | 1.4 / 3–5 ms |
| Internal RAM | 65–69 KB free, **43 KB lowest**, 31 KB largest block |

LVGL costs about 8 points more here than with the 6-track SD list (26 %).
The suspect is the 224-row list, even while hidden behind the waveform.
That isn't confirmed yet.

## 2026-10-31 (later): beat grid on the waveform, XDJ phase meter, honest frame counter

- Beat grid drawn on the waveform from the rekordbox grid: a short tick per
  beat at the top and bottom edges, longer and red on each bar's first beat.
- XDJ-style phase meter above the waveform. Tap it (or serial `v`) to switch:
  - beat display: bar in phrase and beat in bar
  - alignment view: master's and our beat ticks under one playhead
- The beat grid is double-buffered (the screen now reads it too).
- The report now shows real screen fps, plus internal RAM: free, lowest since
  boot, and largest block.
- Waveform height 100 → 80 px to make room for the meter.

Budget (`house.mp3` with the `g` 120 BPM test grid, synced to prolink-cpp's
virtual CDJ at 128 BPM, no USB stick):

| | Varispeed | Keylock |
|---|---|---|
| Screen | 40–42 fps | 40–42 fps |
| Core 0 | 39–42 % | 38–42 % |
| Core 1 | 9 % | 27–28 % |
| Worst audio block (of 5.8 ms) | 1.25 ms | 3.4 ms |
| Real time | 100 % | 100 % |

- **Core 0 by task:** LVGL 25–29 %, loader 8 %, Link 2 %. With the waveform
  off, core 0 is at 20 %, so the waveform plus meter cost about 22 points.
- **Internal RAM:** 93 KB free, 73 KB lowest, 38 KB largest block. A
  224-track stick list costs about 12 KB more.
- **PSRAM:** 19.4 MB free. App: 9 % of the partition free.

## 2026-10-31: Pro DJ Link beat sync with rekordbox beat grids (`0ced6df`)

- Ethernet (IP101): DHCP, with a link-local fallback.
- Pro DJ Link: announces as player 2, sends status, finds the tempo master.
- USB stick mounted at `/usb`. Tracks are listed from their rekordbox ANLZ
  files: `PPTH` gives the audio path, `PQTZ` the beat grid.
- SYNC: matches the master's tempo (half or double time when closer), snaps
  onto its beat at PLAY, then holds phase by bending the tempo up to 3 %.
- SYNC and KEY chips, BPM readout. Keylock moved off the DJ2Go's SYNC button.
- The track list's LVGL objects go to PSRAM: 224 rows had used all internal
  RAM (2 KB left).

Budget:

- **Sync vs the virtual CDJ:** phase error 1.4 / 3.3 ms (avg / worst) in
  varispeed, about 3 / 10 ms in keylock.
- **Link task:** 2–4 % of core 0.
- **Rekordbox track from the stick:** core 0 42–48 %, waveform 34–41 fps.
  This was the old redraw counter, so that drop was at least partly a
  counting artefact.
- **Internal RAM:** 80 KB free with 224 tracks.
- **Boot:** +6.9 s to read 218 ANLZ files.
- **App:** 9 % of the partition free.

## 2026-10-30: keylock fixed, core 0 freed for networking

- `b643e66`: WSOLA overlap-add at unity gain. Keylock had been about 6 dB
  hot and clipping about 13 % of samples. This affects the A1S build too.
- `2bf72a7`: three changes:
  - LVGL sleeps on a semaphore instead of busy-waiting on each SPI flush
  - touch interrupt disabled (SPI clock coupled into its wire)
  - SPI at 40 MHz
- `a74b7c7`: keylock resamples to 44.1 kHz before WSOLA, so 48 kHz files
  play at the right speed and pitch.

Budget:

| | Core 0 | Core 1 | Waveform* | Audio block avg / max |
|---|---|---|---|---|
| Varispeed | 31 % | 8 % | 46 fps | 0.43 / 0.88 ms |
| Keylock | 31 % | 30 % | 46 fps | 1.75 / 3.1 ms |
| Keylock, waveform off | 12 % | 17 % | — | 0.94 / 1.8 ms |

\*Old redraw counter.

- **Before `2bf72a7`:** core 0 98 %, core 1 42 %, waveform 19 fps.
- **Internal RAM:** 173 KB free. **PSRAM:** 19 MB free. **App:** 1.14 MB.
- **Load times:** boot 2.0 s, MP3 0.65 s, WAV 0.13 s.

## 2026-10-29: first P4 deck

- `20df2a6`…`a3b8317`: DJ2Go over USB MIDI. WAV and MP3 stream from SD
  through a 12 MB PSRAM window; MP3 seeks are sample-accurate.
- `88653e4`, `949b500`: the MSP4021 SPI touch screen and the LVGL deck UI.
- `6b68c7f`: scrolling waveform. The I2S interrupt moved to core 1 (it had
  been on core 0, behind the screen's SPI traffic, and audio ran at 80 %).
  Added the health report: real-time %, block timing, click detector.

Budget: real time 78–80 % → 100 % after the interrupt fix, waveform 18–19
fps*, core 0 near 100 % (busy-wait flush, fixed the next day).

\*Old redraw counter.
