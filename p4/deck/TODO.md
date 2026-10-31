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

## XDJ-1000 parity roadmap

In order. Costs: **cheap** = logic only, no per-sample work; **moderate**; **heavy**.

1. **Performance basics** (cheap; in progress)
   - [x] Hot cues A–H and memory cues read from rekordbox, plus CALL ◀ ▶, the
     Bars countdown, and cue marks.
   - [x] Loops: beat loop 1–16, manual in/out, reloop/exit, halve/double.
   - [x] Beat jump ±4 (it moves an active loop with it).
   - [x] Quantize.
   - [x] Setting memory cues on the deck (MEM / DEL), for the session only.
   - [ ] Write cues set on the deck back to the stick, as an XDJ does.
   - [x] Tempo range ±6/10/16/WIDE, tempo reset.
   - [x] Slip mode. Reverse (always varispeed).
   - [ ] Vinyl-mode jog. Needs a touch-sensitive jog, so it waits for the I2C panel.
2. **Full Link citizen**
   - [x] Send beat packets, take tempo master, master handoff (both ways),
     remote sync on/off.
   - [x] Mixer integration: on-air, fader start. **Untested**: needs a DJM.
   - [ ] Test against the real XDJ-1000MK2: sync both ways, master handoff
     both ways, and by-ear phase trim.
   - [ ] Load from other players' media: network file client plus
     `export.pdb` (heavy).
   - [ ] Let other players load from our stick: file server plus rekordbox
     database server (heavy).
3. **Library and browsing**
   - [ ] Parse `export.pdb`: playlists, artist, BPM, key, search, history;
     track info on screen (moderate).
   - [ ] FLAC and AIFF (cheap–moderate), AAC/M4A (moderate).
   - [ ] Faster stick loads (index built in the background), cache the track
     list on the stick, load the player settings saved on the stick.
4. **Screen**
   - [ ] rekordbox colour waveforms (moderate), waveform zoom (cheap).
   - [ ] The XDJ layout (BROWSE, TAG LIST, INFO, MENU, PERFORM) on the DSI
     screen.
5. **Hardware**
   - [ ] PCM5102A stereo line out.
   - [ ] I2C control panel: jog, 14-bit pitch, pads.
   - [ ] DSI screen.
6. **Undecided: on-deck track analysis** (for tracks rekordbox hasn't analysed)
   - *Why:* SYNC, quantize, beat loops, beat jump and the phase meter only
     work on rekordbox tracks today. SD tracks and plain USB folders get none
     of it. A grid made on the deck would make all of it work for any file,
     without rekordbox.
   - *Fits?* Probably. Core 1 is 70–90 % idle beside the audio and core 0
     about 50 %. Onset envelope plus tempo and phase estimation is cheap; the
     cost is decoding the whole file. Rough guess, unmeasured: 10–30 s per
     track in the background, mostly reading at USB full speed. Cache the
     result on the card or stick so it runs once per track.
   - *Risks:*
     - Accuracy: tempo and phase for steady 4/4 dance music are fairly easy;
       the downbeat (which beat is "1") is hard. freedj-3000 is off by 1–3
       beats on bars.
     - PSRAM contention could slow the audio; measure with the health report.
     - A wrong grid is worse than none, so it needs a grid adjust (shift the
       grid, tap the downbeat), like the XDJ's GRID ADJUST.
   - *Options, cheapest first:*
     1. Overview waveform only, for non-rekordbox tracks. No accuracy risk.
     2. Constant-tempo grid (BPM + phase), cached, plus grid adjust.
        Suggested choice.
     3. Varying-tempo grids, key, phrase detection. Bigger; pays off mostly
        outside dance music.
   - *If yes:* do it after item 3 (browsing and loading). Test it on the SD
     tracks, and on rekordbox tracks with their grid hidden, comparing the
     deck's result with rekordbox's.

## Features
- [ ] **Pro DJ Link, next steps** (following the master works; see README):
  - Test against the XDJ-1000MK2 and trim the phase by ear (`[` `]`).
  - Send beat packets so the deck can be tempo master, and handle master handoff.
- [ ] **Waveform got slower with a stick track**: 34–41 fps (was 46), core 0 at 42–48 %. It isn't the allocator. Suspects: USB stick reads on core 0, the 224-row list.
- [ ] **One keylock block took 5.1 of its 5.8 ms** while a track was still loading. Find out why.
- [ ] **LVGL costs ~8 points more with the 224-track list** (34 % vs 26 %), even with the list hidden behind the waveform. Confirm, then build only the visible rows (a virtual list).
- [ ] **Re-plugging the stick during an emergency loop** could pick the track back up where it is. Today the loop runs until another track is loaded.
- [ ] **Boot +12–14 s** reading 218 ANLZ files at full speed: cache the track list on the stick.
- [ ] **Slow loads with the stick at full speed** (needed for the hub): MP3 load 4.9 s, because building the index reads the whole file. Build the index in the background once the track is ready to play.
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
