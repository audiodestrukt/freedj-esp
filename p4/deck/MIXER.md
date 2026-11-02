# Mixer build (plan)

Status: plan only, nothing built yet (2026-11-02).

One box that mixes two decks. It has one jog platter in the centre, and each deck's mixer
controls run down its own side: deck A on the left, deck B on the right, the same sides they
take in the mix. It uses the same firmware and the same jog module, screen mount and button
hardware as the CDJ builds in [BUILDS.md](BUILDS.md). It's a third case layout.

## The rule: what each deck gets, what switches

During a blend you ride both decks at once, so anything you touch in a mix comes in a pair.
Anything you use on one deck at a time follows the **A/B toggle**.

| Each deck has its own (left and right edges) | Follows the A/B toggle (centre) |
|---|---|
| EQ HI / MID / LOW | Jog: scratch, nudge, search |
| GAIN or FILTER | Pitch (unless there's one per edge, see below) |
| Channel fader | Loops, hot cues, beat jump, quantize |
| Headphone cue (PFL) | Browse and load (loads into the selected deck) |
| CUE and PLAY | |

CUE and PLAY come in pairs because you start B while you're still riding A. That's the moment
you don't want to flip a switch first.

## Layout

```
 ┌─────────────────────────────────────────────────┐
 │ [GAIN]        ┌─────────────────┐        [GAIN] │
 │  (HI)         │     screen      │         (HI)  │
 │  (MID)        └─────────────────┘         (MID) │
 │  (LOW)          A ◄─[toggle]─► B          (LOW) │
 │ [FILT]          ╭───────────────╮        [FILT] │
 │                 │               │               │
 │                 │    platter    │               │
 │                 │               │               │
 │ [CUE][PLAY]     ╰───────────────╯   [CUE][PLAY] │
 │ [PFL] ║ch A║   ═══[ crossfader ]═══   ║ch B║ [PFL] │
 └─────────────────────────────────────────────────┘
```

- **Screen** at the top centre, as today. The selected deck's scrolling waveform is large; the
  other deck is a strip (overview, BPM, time left, phase). Each side has its own colour, which
  also shows which deck the toggle has selected.
- **A/B toggle** between the screen and the platter, where your eyes already are.
- **EQ columns** on the edges: 4 knobs on about 20 mm centres. Today's case already leaves
  about 28 mm beside the 104 mm jog well (W = 160), so a column fits with the case a little
  wider.
- **CUE/PLAY** in the bottom corners, under each column: where your thumbs land, and away
  from the platter, so no accidental jog touches.
- **Faders** in a strip under the platter: channel A, crossfader, channel B.

### Size

- **Width:** about 180–200 mm (jog well 104 + two ~35 mm columns + walls). Today it's 160.
- **Depth:** today's depth + about 35 mm for the fader strip.
- **Height:** from the deepest part, a 45 mm fader or a pot with its shaft. Check it against
  the flat case's 38 mm, as with the buttons in BUILDS.md.

## Open choices

1. **Pitch: one modal fader, or one per edge?**
   - **One** in the centre column or beside the platter, with soft takeover (after a switch it
     does nothing until it passes that deck's pitch). Smaller. You set the incoming deck's
     tempo once anyway, or SYNC does it.
   - **One per edge:** 45 mm faders don't fit in a column under 4 knobs without making it
     much deeper (4 × 20 + 60 + 24 ≈ 165 mm). A 30 mm fader might.
   - Leaning: **one modal fader**, with SYNC.
2. **Faders or knobs** for the channel volumes? 45 mm faders are what a mixer feels like; knobs
   keep it small. Leaning: **faders**, with a 45 mm crossfader.
3. **Screen:** the 4" MSP4021 works. The DSI screen would fit two waveforms at once.
4. **GAIN or FILTER** for the 4th knob on each side. A filter is the more useful one in a
   mix. Leaning: **FILTER**, and keep gain on the screen, auto-set from the track's loudness.

## Inputs and pins

Rough count, with one pitch fader:

| Analog | Count |
|---|---|
| EQ HI/MID/LOW + FILTER, × 2 | 8 |
| Channel faders | 2 |
| Crossfader | 1 |
| Pitch | 1 |
| Headphone cue mix or level | 1 |
| **Total** | **13** |

| Digital | Count |
|---|---|
| CUE, PLAY, PFL, × 2 | 6 |
| A/B toggle | 1 |
| SHIFT or LOAD (optional) | 1 |
| **Total** | **7–8** |

The P4 doesn't have 13 usable ADC pins on the headers. Two ways:

- **No custom board:**
  - analog: a **CD74HC4067** 16-channel mux on 1 ADC pin plus 4 select pins;
  - buttons straight to GPIO with pull-ups;
  - about 12–13 P4 pins in all;
  - or an **MCP23017** on the I2C bus for the buttons, which shares the jog's I2C pins and saves 7–8 pins.

  The mux is read in a round-robin: settle, read, filter (the same IIR filter + deadband
  planned for the pitch pot in BUILDS.md). At 13 channels × 200 Hz that's 2.6 k reads/s: trivial.
- **Control board:** the CH32V003 board reads everything and sends events over I2C. Tidier
  wiring. With this many controls it's the better reason to build that board.

## Audio

- **Master:** the codec line out, as today.
- **Headphone cue:** a second output, a **PCM5102A** on a second I2S port (the P4 has spare).
  The same part the two-deck plan uses for deck B. The headphone mix knob blends cue and master.
- **Speaker:** master, with the MENU on/off option from TODO.md.
- **USB sound card** (XDJ, DJ2Go): today it gets a copy of the output. It could carry cue instead.

### DSP per sample, on core 1

1. Each deck: varispeed or keylock (existing).
2. Each deck: 3-band EQ, then the filter. The EQ is cheap if it's done like Pajoniiir's (one-pole
   splits at about 800 Hz / 4 kHz, float). Steeper biquads (DJM-like, with full kill) are about
   20 multiplies per sample per deck. Either is small.
3. Channel fader and crossfader gains, a sum, then a soft limiter on master.
4. The cue bus: the decks whose PFL is on, before their faders.

**Budget (estimates, measure as it lands):**
- Core 1: two keylock decks about 50 % today. The EQ, filter and mix add a few points, so
  about 55–60 %.
- Making keylock cheaper is on the list: coarse-to-fine or SIMD search, and fixed-point
  playhead positions instead of per-sample double.
- Core 0: a second decoder, a second waveform strip and the mux reads. Measure the MP3 decode.

Floats for the mixer math (the P4 does single-precision float in hardware). The stored audio
stays 16-bit.

## Firmware

Same image as the CDJ builds. What the mixer build adds is turned on when its panel is found
(the 0x42 probe or the mux wiring in `board.h`):

- the second deck: TODO.md's two-deck plan, steps 1–3;
- `mixer.cpp`: EQ, filter, faders, crossfader, limiter, cue bus. Host-testable like
  `wsola.cpp` (`native/`);
- `panel_mux.cpp`: mux scan, filtering, soft takeover, button debounce. Sends the same
  `deck::Event`s plus new mixer events;
- the controller mappings: the DJ2Go already sends both decks' volumes, the crossfader, master and
  phones levels (`src/dj2go.cpp`), so most of the mixer can be tested with it before any hardware
  exists. It has no EQ knobs: test EQ and filter from serial keys or the screen until the panel is
  built.

## CAD

`FREEDJ_CONFIG = "mixer"`, next to `angled` and `flat`:

- **Reuses:** the jog module (shaft, bearings, AS5600 mount, platter), the screen mount and
  window, the panel-button holes.
- **New:**
  - the EQ column holes: 4 × knob pots on each side, with an anti-rotation tab slot;
  - the fader slots for 2 channel faders + crossfader (+ pitch);
  - the toggle hole;
  - the PCM5102A board's mount, and its 3.5 mm or RCA jack on the back.
- **The clash check** includes the pot and fader bodies and the mux board.

## Order of work

1. **Firmware with no new hardware:**
   1. two decks;
   2. `mixer.cpp` with host tests;
   3. drive it from the DJ2Go's volumes and crossfader, EQ from serial keys.

   This proves the CPU budget and the sound.
2. **PCM5102A cue output** on a second I2S port.
3. **Panel on the bench:** CD74HC4067 + pots/faders on a breadboard, `panel_mux.cpp`, soft takeover.
4. **CAD:** the `mixer` config. Print the top plate only to test the hole spacing and feel.
5. **Full case**, wiring table, BOM in hardware/README.md.

## Parts (beyond the CDJ build)

| Part | Qty | Note |
|---|---|---|
| B10K linear pots, 9 mm or 16 mm, D shaft | 9 | EQ + filter × 2, headphone mix. A centre-detent version for EQ and filter is nicer. |
| 45 mm slide pots, B10K | 3–4 | 2 channel faders, crossfader, + pitch if it's per edge |
| Panel buttons, 16 mm flat-head momentary | 4 | Second CUE/PLAY pair (the first pair is the CDJ build's), + PFL × 2 |
| SPDT on-on toggle | 1 | A/B |
| CD74HC4067 breakout | 1 | Analog mux |
| PCM5102A board | 1 | Headphone cue out |
| 3.5 mm or 6.35 mm headphone jack | 1 | |
