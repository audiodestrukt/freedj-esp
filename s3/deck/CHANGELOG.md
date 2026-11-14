# s3/deck changelog

## 2026-11-14 (later): playable, 60 fps on vsync

- **Plays**: freedj-nano's deck engine (`components/nano`, the C6's: transport, CDJ cue, jog
  nudge / scratch, keylock) as an IDF component from the sibling checkout, tracks from a
  `tracks` partition (0x310000, 12.9 MB) packed by nano's `pack_tracks.py`
  (`tools/flash_tracks.sh`; 3 x 40 s at 44.1 kHz mono fit with room to spare). Audio task on
  core 1 into the PCM5101 and speaker; I2S now BCK = 64 fs (32-bit slots) as the PCM510x needs
  without MCLK.
- **Touch** (SPD2010 touch side, ported from Waveshare's demo: Espressif's component failed its
  version read on the I2C v2 driver). Jog ring: hold to stop, turn to scratch (1.8 s a turn);
  drag the waveform to scratch; tap the title for the next track; CUE / PLAY as on a CDJ. The
  touch task is the deck's control task.
- **Vsync**: the panel's TE line (GPIO18) runs at 60 Hz; frames start on it.
- **Ring**: the dots are a fixed scale; only the line turns (the platter, 33 1/3 rpm, so it
  follows playing and scratching). 3-band waveform from the pack (CDJ-3000 layering).
- **Speed**: each 32-row band drawn by both cores (16 rows each) while the last one sends;
  dot masks, the waveform's circle clip and the overview worked out once (`round_ui::prepare`);
  edge AA without sqrt; rasterisers in IRAM (from flash they missed the cache while audio
  streams the track from flash): frame 23.4 -> **13.9 ms (14.5 worst), 60.0 fps locked to vsync**.

## 2026-11-14: the S3 deck starts, with a round UI

- New project beside p4/deck for S3 boards. First board: Waveshare ESP32-S3-Touch-LCD-1.46
  (ESP32-S3R8, 16 MB flash; SPD2010 412×412 round QSPI LCD with touch; PCM5101 into a
  speaker, no line out; TCA9554 expander for the LCD/touch resets and SD D3; power latch on
  GPIO7). Pins in `main/board_ws146.h`, from Waveshare's wiki and demo.
- Bring-up: power latch, I2C (touch 0x53, RTC 0x51, IMU 0x6B answer), LCD, backlight PWM,
  I2S at 44.1 kHz (a tone played).
- Round UI (`main/ui/`), plain C++ shared with a PC preview (`make -C host`). One layout for
  412 and 240 px (the puck).
- Speed, 412 px: a full frame sends in 10.3 ms. First draw 84 ms (11 fps): per-pixel float maths
  is slow on the S3 (multiply-add ~11 cycles, sqrtf ~80, floorf ~60). Overview cached per
  track (5.2 -> 0.6 ms), rings filled per row with AA only on edge pixels, strobe dots instead
  of 48 AA lines (jog ring 22.6 -> 7.0 ms), title fit cached (top 7.7 -> 2.7 ms), integer
  floor/ceil, -O2, QIO flash: **23.4 ms drawing, 38.8 fps**.
