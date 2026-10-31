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
