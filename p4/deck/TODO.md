# P4 deck — TODO for next session

Where things stand (2026-10-30):
- WAV/MP3 streaming, varispeed and keylock (48 kHz files included), CUE, jog, the touch UI and a scrolling waveform all work.
- Audio is at 100 % real time.
- Playing with the waveform up: core 0 at ~30 %, core 1 at ~8 % (varispeed) or ~30 % (keylock), waveform at ~46 fps.
- Background is in [README.md](README.md) and on the blog ([the waveform was stealing my audio](https://audiodestrukt.com/blog/posts/esp32-deck-waveform-starved-the-dac.html), [status and CPU headroom](https://audiodestrukt.com/blog/posts/esp32-p4-deck-status.html)).

## Check first
- [ ] **Re-run the A1S hardware suite** (`tools/run_tests.py`, board on `/dev/ttyUSB0`).
  - The WSOLA gain fix (`b643e66`) makes A1S keylock ~6 dB quieter, and keylock is the A1S timecode default.
  - Confirm timecode lock and steering still work with real vinyl.
  - See whether the DAC volume cap (vol=50, added for DAC→ADC crosstalk) can come up now.

## Features
- [ ] **Pro DJ Link, next steps** (following the master works; see README):
  - Test against the XDJ-1000MK2 and trim the phase by ear (`[` `]`).
  - Send beat packets so the deck can be tempo master, and handle master handoff.
- [ ] **Waveform got slower with a stick track**: 34–41 fps (was 46), core 0 at 42–48 %. It isn't the allocator. Suspects: USB stick reads on core 0, the 224-row list.
- [ ] **One keylock block took 5.1 of its 5.8 ms** while a track was still loading. Find out why.
- [ ] **LVGL costs ~8 points more with the 224-track list** (34 % vs 26 %), even with the list hidden behind the waveform. Confirm, then build only the visible rows (a virtual list).
- [ ] **Internal RAM low-water mark is 43 KB** with the stick list. Find the biggest internal users (task stacks, LVGL draw buffers, lwIP) before adding features.
- [ ] **Boot +6.9 s** reading 218 ANLZ files: cache the track list on the stick.
- [ ] **USB hub never appears** in either H3 position, so only one USB-A port works (stick or DJ2Go).
- [ ] **Deck B and the crossfader.** The DJ2Go already sends deck B events, so this needs a second `TrackStream` and `Deck`, plus mixing.
- [ ] **Overview waveform**: the whole track in one strip. Analyse in the background after load, and cache the peaks on the SD card so each track is only analysed once.

## Audio quality
- [ ] **Keylock phasing while the pitch fader moves** (steady pitch is fine).
  - The DJ2Go fader is 7-bit, so speed changes in 0.125 % jumps.
  - Try first: glide the speed toward the fader value over ~50–100 ms in the audio task. That helps varispeed too.
- [ ] If that isn't enough, tune WSOLA:
  - Search wider (±2.9 ms now; the bass needs ±8–12 ms).
  - Match over the whole overlap, not just the first hop.
  - Use normalised cross-correlation instead of AMDF.
  - Try 2048-sample frames.
  - A/B on the PC with headphones (the tiny speaker hides it).
  - Phase vocoder is the fallback; core 1 has the room.
- [ ] **A longer `sine.wav`** (a minute or more) so the click detector covers a real run, not 2 s.

## Screen
- [ ] **Optional**: software vsync on the MSP4021. Wire SDO (MISO), read the scanline (ST7796S `0x45`), and start each waveform write behind the scan. Only worth doing if tearing bothers us before the DSI screen arrives.
- [ ] **DSI screen** when it arrives. It has real vsync via ESP-IDF's DPI driver, and the LVGL UI moves over as is.
- [ ] **Maybe**: cap the waveform at 30 fps to give back ~8 % of core 0, if Link needs it.

## Hardware, later
- [ ] PCM5102A stereo DAC, and a PCM1808 ADC for timecode input on the P4.
- [ ] I2C control panel to replace the DJ2Go, with a 14-bit pitch control.
