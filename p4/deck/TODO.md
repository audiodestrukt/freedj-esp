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
   - [x] rekordbox colour waveforms (PWV5), scrolling and overview.
   - [ ] Waveform zoom (cheap). TAG LIST, INFO, MENU screens.
   - [ ] MENU option: **speaker/line out on or off.** Today it always plays, and a USB
     sound card (e.g. an XDJ in control mode) gets a copy (`audio_out::write`). Mute by
     writing zeros to I2S, not by a codec mute over I2C (that collided with touch reads).
   - [x] Bigger app partition: 8 MB (`partitions.csv`), 83 % free.
   - [ ] The XDJ layout (BROWSE, TAG LIST, INFO, MENU, PERFORM) on the DSI
     screen.
5. **Hardware**
   - [ ] **3D-printed enclosure**, first design in [hardware/](hardware/README.md): a small CDJ
     around the 4" MSP4021, jog on two 608s + AS5600, CUE/PLAY, 45 mm pitch, angled and
     flat-top versions. Next: caliper the screen and the dev board's port heights, print and test the jog module alone, choose jog-touch sensing.
   - [x] **Get the jog's feel back.** It read ~±100 counts at rest after the reassembly
     (2026-11-02: DIR had come off GND, and the speaker sat next to the sensor), so a 1 Hz /
     3-tick filter hid it but felt slow. 2026-11-05: `R` at rest reads 0 counts of spread, and
     the filter is loosened to 30 Hz / 1-tick deadband (first tick 88 -> 38 ms in
     `tools/jog_filter_sim.py`-style simulation). Still to check: `R` with audio playing on the
     speaker near the sensor (it was muted for the USB interface), and the feel by hand.
   - [ ] **Release 1 with no custom boards**: panel-mount buttons + pot straight to P4 GPIO,
     control board kept as the later build. Plan in [BUILDS.md](BUILDS.md).
   - [ ] **Mixer build**: one platter in the centre, each deck's EQ, filter, CUE/PLAY and PFL down
     its own edge, faders under the platter, A/B toggle for the jog. Plan in [MIXER.md](MIXER.md).
   - [ ] **Touch back with the AS5600 on I2C.** The encoder took pins 3/5 and the screen's
     touch is unplugged for now (DJ2Go covers it). Either double up both on pins 3/5
     (0x36 and 0x38 share fine), or give the encoder its own bus; for good once the
     control board carries it. Also: print the `jog_test_*` gap pieces (magnet is
     Ø4 × 2, now in `MAGNET`), choose a gap by AGC and the `R` spread, set `AIR_GAP`.
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
- [ ] **Sync bend smoothing: probably too much, cut the lag** (added 2026-11-04 at
  `kSyncBendSmooth` 0.02/block, ~0.3 s, after the tempo display jumped against an
  XDJ-1000MK2; the display fix alone may have been enough). Measure the phase error
  (serial sync stats, avg/max ms) and the bend's spread with smoothing off, 0.1 and
  0.02, on a direct cable. Then use the least that keeps it inaudible in varispeed,
  possibly none. Lag matters more: also check whether the beat-packet timestamp jitter
  is ours (link task priority, select wake-up) before filtering it.
- [ ] **Waveform got slower with a stick track**: 34–41 fps (was 46), core 0 at 42–48 %. It isn't the allocator. Suspects: USB stick reads on core 0, the 224-row list.
- [ ] **One keylock block took 5.1 of its 5.8 ms** while a track was still loading. Find out why.
- [ ] **LVGL costs ~8 points more with the 224-track list** (34 % vs 26 %), even with the list hidden behind the waveform. Confirm, then build only the visible rows (a virtual list).
- [ ] **Re-plugging the stick during an emergency loop** could pick the track back up where it is. Today the loop runs until another track is loaded.
- [ ] **Boot +12–14 s** reading 218 ANLZ files at full speed: cache the track list on the stick.
- [ ] **Slow loads with the stick at full speed** (needed for the hub): MP3 load 4.9 s, because building the index reads the whole file. Build the index in the background once the track is ready to play.
- [ ] **Two decks, one control surface, an A/B toggle** (plan 2026-11-02). One jog, one set of
  buttons and one pitch fader drive whichever deck the switch selects. The DJ2Go already sends
  deck B events.
  - **Switch:** on-on toggle to one GPIO with a pull-up (a 4th `panel_gpio` pin, BUILDS.md). The
    lever shows the deck, so no LED. Serial `Tab` does the same thing for testing.
  - **Routing:** jog, buttons, XDJ/DJ2Go go to the selected deck. The **pitch fader needs soft
    takeover**: after a switch it does nothing until it passes that deck's pitch (same for XDJ tempo).
  - **Audio out, recommended:** separate outputs into an external mixer, CDJ-style. A on the codec
    line out, B on a second I2S DAC (PCM5102A) or a USB sound card. No crossfader/EQ in firmware.
    An internal mix would also need a cue output, so a second output either way.
  - **Screen:** selected deck's scrolling waveform large; the other as a strip (overview, BPM,
    time left, phase). Two scrolling waveforms would roughly double LVGL's core 0 cost.
  - **Budget (estimates):** core 1 with two keylock decks about 50 % (one is 27–28 %). Core 0 gets a
    second decoder: measure the MP3 decode. PSRAM: two 12 MB rings fit in 32 MB but are tight;
    drop to about 8 MB each (~47 s).
  - **Link:** two player numbers; SYNC can lock B to A inside the box.
  - **Order:**
    1. a second `TrackStream`/`Deck`, B mixed to the right channel, `Tab` to switch. Measure;
    2. soft takeover;
    3. screen A/B header + strip;
    4. second I2S DAC for B;
    5. the switch: `panel_gpio`, CAD hole, BUILDS.md.
  - **Keylock cost, if needed:** coarse-to-fine search (4× decimated, then ±4 at full rate) or
    compare every 4th sample. About 10× cheaper. Pajoniiir (another P4 dual-deck project)
    compares only 2 samples over 15 candidates to fit two decks; that's too few for good sound.
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

## Timecode vinyl (working 2026-11-05, USB interface)
- [ ] **Start on record motion**: play when the record moves, no PLAY press (Serato relative). Decide what CUE means while the record drives.
- [ ] **33/45 setting** (or detect it from the carrier): 45 plays 1.35x fast.
- [ ] **Scratch feel** with a hand on the platter: hand-to-sound latency, `kVinylSmooth`, the keylock handover.
- [x] **Needle drops** (2026-11-05): hold silent until bit lock (0.1-0.5 s), track 0:00 = the first groove (24.0 s of timecode), small errors re-synced; within ~25 ms in steady play.
- [ ] **The screen catches up after a needle drop**: the audio lands right, but the waveform/time visibly re-sync a moment later. Look at what the UI does on a big seek.
- [ ] **CPU when nothing's playing in**: gate the decoder on input level (noise floor ~19 vs carrier threshold 600), build the 3 MB table on first carrier, and a `CONFIG_FREEDJ_VINYL` build option.
- [ ] Lock misses: 3 of 12 drops got no bit lock (two lifted again within 50-160 ms, one ran out 1 s).
- [ ] **Screen**: the record's speed in place of the fader's, and a signal / bit-lock mark.
- [ ] Serato Control CD format, for CDJs playing timecode CDs.

## Hardware, later
- [ ] PCM5102A stereo DAC. (Timecode input: done over a USB audio interface; a PCM1808 on I2S is only worth it for a build without one.)
- [ ] I2C control panel to replace the DJ2Go, with a 14-bit pitch control.
