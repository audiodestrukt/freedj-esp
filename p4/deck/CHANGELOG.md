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

## 2026-11-01: AS5600 jog encoder works (I2C)

- New `main/as5600.cpp`: the AS5600 drives the jog. It sends the same
  `Jog` events as the DJ2Go's jog, at 180 ticks per turn (1.8 s of track,
  about a record at 33 1/3), so scratch and nudge work unchanged and the
  DJ2Go still works alongside it.
- **I2C** at 0x36 on the codec/touch bus (SDA pin 3, SCL pin 5), polled at
  500 Hz from a core-0 task (never the audio task). Magnet status is checked
  every 100 ms; samples without a magnet are skipped and the position
  re-seeds, so it can't jump. Whole-tick hysteresis.
- **Analog fallback** on pin 7 (GPIO 23) if nothing answers at boot: 2 ADC
  reads per sample, smoothed (13 ms), range learned from what's seen.
- Bench numbers: at rest ±1 count on I2C against ±40 on the ADC; 0 drift
  ticks at rest; one read 177 us (bus wait, core 0); analog was 282 us per
  sample. Turning the platter tracked both ways through the wrap.
- What went wrong on the way:
  - The ADC task first hogged core 0. Eight reads took more than 1 ms, so
    `vTaskDelayUntil` never blocked and the idle task's watchdog fired. It
    now always yields.
  - The magnet on the printed shaft was too far away: AGC 128, magnitude
    ~148, "too weak". It was solid at ~1 mm (AGC 0, ~2,080).
  - The analog OUT never moved even with the magnet seen, most likely the
    wire on GPO (internal pull-up, reads 3.3 V) or OUT not soldered.
- Serial keys: `e` encoder readings (mode, raw, ticks, lost samples), `E`
  magnet status over I2C (detected / weak / strong, AGC, magnitude, angle,
  output config).

## 2026-11-01: Case fixes from the first print (CAD only)

- Optional Pioneer-style platter, `cad/pioneer_platter.py`. It's freedj-3000's
  `jog_wheel.py` profile (dished top, hollow grooved skirt, hub), resized to
  this deck:
  - 100 mm across, 10 mm tall, so it sits in the 104 mm well where the
    knurled platter does.
  - The dished centre is the touch plate's seat (80.4 mm, 1.2 mm deep).
  - The skirt wall is 3.8 mm, so its inside locates the steel inertia ring
    (0.2 mm clearance). The ring sits up against the top plate.
  - Three 0.8 mm grip grooves (freedj-3000 has four of 1.2 mm).
  - It keeps jog.py's printed shaft and inner-race spacer, so the bearings,
    magnet and AS5600 don't change.
  - 35 cm³ against the knurled platter's 61. One body, watertight, in both
    configs.
  - `FREEDJ_PLATTER=pioneer` puts it in `machine.py`'s assembly. With it, the
    platter and ring have 0 clashes with every other part, angled and flat.
  - Print it like the knurled one: upside down, shaft up, supports only in
    the centre recess.
- Jog shaft is a full 8.0 mm (was 7.95), for both platters: 7.95 printed
  loose in the 608 bearings. `fit_gauge.stl` now has pins of 7.95, 8.00, 8.05
  and 8.10 (was 7.90 to 8.05). Reprint: platter (whichever one you use).
- Pioneer platter reshaped to match Dan's reference mesh
  (meshy_1788016598940.glb, a disc with a 6 mm band, a dimpled shoulder and a
  domed top):
  - The top is flat (the reference's dome flattened). Its centre is still the
    touch plate's seat (80.4 mm, 1.2 mm deep), with a flat land out to r 45.
  - The base band is thinner: 2.5 mm, with one fine line (the reference's is
    about 3.3 at this scale, with two).
  - Shoulder: a 6 mm round off the band, then 45 degrees up to the top. It has
    24 oval dimples (12 x 8 mm, 1.1 deep). The 45 degrees is what lets it
    print upside down with no support; the reference's shoulder flattens out
    at the top, which wouldn't.
  - The inertia ring now sits flush with the skirt's foot (z 0-5). The hollow
    is 2.6 mm from the deepest dimple.
  - 37.7 cm3. One body, watertight, angled and flat. 0 clashes in both
    shells with `FREEDJ_PLATTER=pioneer`.
- Magnet pocket in the shaft tip is now Ø6.05 (was Ø6.16), a light press fit
  (`MAGNET_FIT`). At Ø6.16 the magnet fell out before the E6000 set.
  Reprint: platter.
- Rear USB-A slots 0.5 mm wider (0.75 mm clearance per side, was 0.5): the
  ports' metal shells flare at the mouth and had to be filed to fit. Height
  unchanged. The web between the two slots is 3.4 mm, and 3.1 mm to the
  Ethernet opening. Reprint: shell.

## 2026-11-01: Populated control board test fit (CAD only)

- New `cad/ctrl_fit.py`. It takes the populated board from tscircuit's GLB
  export, which has the real JLCPCB/EasyEDA part models. tscircuit's STEP
  export can't merge those models. It registers the board on its own origin
  (deck CTRL_XY centre, top face at CTRL_Z), not a bounding box.
- The switch model is wrong: ZX-QC66-7.5TP is drawn 5.0 mm tall, but the
  part is 7.5 mm (LCSC). Its body is replaced with 6 x 6 x 3.5 plus a
  Ø3.5 actuator to 7.5 mm.
- Outputs: `build/ctrl_board_pop{,_flat}.stl` (deck frame, drops onto the
  shell STLs), `build/ctrl_board_pop_local.stl` (board alone, centre 0,0,
  top face z 0), and render scenes.
- Clash check, every part's convex hull against the shell, rings, caps and
  base: **0 clashes** in both configs with lit and plain buttons. Checked
  that it does report clashes: raising the board 1 mm hits the bosses,
  skirts and caps.
- Renders 0034 (populated board) and 0035 (section in the deck).
- `build/ctrl_board_print.stl`: a printable stand-in for test fits. The real
  1.6 mm plate with its screw holes, every top-side part as its convex hull
  (switches at 7.5 mm), no connectors. One watertight body, 42 x 58 x 9.1 mm,
  flat on the bed.

## 2026-11-01: Control board PCB (CH32V003 I2C target), routed and DRC-clean

- New `hardware/ctrl-pcb/`, a tscircuit design, 42 x 58 mm, 2 layers. A
  **CH32V003F4P6** (C5187096: 26k in stock, ~$0.29) is the I2C target for
  the fader ADC, button debouncing and 8 individually driven ring LEDs.
  The ATtiny1616/1626 had only 47-108 in stock. The AS5600 shares the bus
  through J_ENC.
- All SMD on top (one-side assembly): 2 x ZX-QC66-7.5TP 6 x 6 x 7.5 tacts
  (wired on diagonal pads), 4 x 0805 LEDs per ring on r 11.3 (green
  C2297 / 100R, amber-yellow C2296 / 470R), a resistor per LED inside the
  skirt, the MCU and passives right of the buttons. Bottom: JST-XH for
  the host (back edge), AS5600 and fader (right edge), and SWIO pads.
- The anchors come from the CAD (`gen_geom.py` -> `lib/geom.json`), so the
  board can't drift from the case. `build.sh` does placement, Freerouting +
  capacity router, island removal, the TraceMaker finish, the gates and
  `fab/`.
- Result: KiCad DRC clean (0 shorts, 0 open), one GND pour island, JLCPCB
  DFM OK, no floating pads, no parts under the ring skirts or the bosses.
  BOM: 27 parts in 11 lines, LCSC numbers from the design. 3 connectors to
  hand-solder.
- CAD follows the real parts: TACT 12 x 12 x 7.3 -> 6 x 6 x 7.5 (board
  0.2 mm lower), connector positions and J_ENC to 5 pins, skirt notches
  3.6 mm wide to clear the 0805 pads. Clash check clean in both configs
  with both button styles.
- Firmware for the CH32V003 isn't written yet.

## 2026-11-01: CDJ-style tempo knob (CAD only)

- The slider knob was a plain rounded block. It's now a CDJ-style tempo
  knob: 14 mm across the slot, tapering from an 11 mm base (1.2 mm straight
  foot) to a 12 x 5.5 mm flat top, 10 mm tall, with concave finger scoops
  (R 14, 1.3 mm deep) front and back, a 0.6 mm round on the top edges, and
  the index line across the top centre. Same lever socket, so the fit is
  unchanged.
- Clash check clean in both configs; watertight, one body.
- Reprint `slider_knob.stl`. Render 0033.

## 2026-11-01: Plain buttons, without light rings (CAD only)

- New `play_cap_plain.stl` / `cue_cap_plain.stl`. A Ø22 cap fills the
  top's Ø22.4 hole, and its Ø23.6 flange (chamfered 45° on top, 0.2 mm
  under the top at rest) catches the underside. It has the same Ø4 nub
  onto the same tact switch, and uses the same shell and control board,
  so the LEDs are optional. A translucent plain cap picks up the LEDs'
  light through its flange.
- `FREEDJ_BUTTONS=plain` (default `lit`) puts them in the assembly and
  the clash check. Both styles are clean in both configs; all printed
  parts are watertight, one body.
- Render 0032 (plain buttons, section).

## 2026-11-01: Buttons held in by the control board; 60 mm pitch fader (CAD only)

- **Buttons:** cap and light ring both go in from below, and the control
  board holds them in. The ring gains a Ø23.6 skirt (1 mm wall) from the
  top's underside down to the board; the board clamps it, and it carries
  the LED light up to the ring (notches over the LEDs). The cap gains a
  Ø21 flange under the ring tube, 0.2 mm below it at rest, chamfered 45°
  on top so it prints upside down, and a Ø4 nub onto the tact switch. The
  shell's guide sleeves are gone: a plain Ø22.4 hole. The skirts are
  0.4 mm apart (buttons 24 mm apart).
- **Control board:** 42 x 58 mm (was 40.5 x 56.5), grown 1.5 mm at the
  front and left so the skirts sit fully on it. The LEDs move to r 11.3,
  tangential, under the skirt wall.
- **Pitch fader: PTA6043-2015DPB103** (60 mm) instead of the PTA4543.
  75 mm frame, M2 holes 71 apart, slot 64.6 mm (still 2 mm to each
  screw). In the angled version the frame's end is 1.5 mm into the dev
  board's footprint in plan, 9 mm above its tallest part there.
- The tick scale (long ticks 3.5, short 2) and the tempo-zero LED move
  in from the deck's right edge; they ran onto the 2 mm edge round.
- Clash check over every new part in both configs: clean (only the
  knob's inlaid line, as intended). All printed parts watertight, one body.
- Print: shell, `play_cap`, `cue_cap`, `light_ring` x 2, `slider_knob`.
- Renders 0030 (button section), 0031 (60 mm fader).

## 2026-11-01: PLAY cap exported under its own name; light ring exported

- `controls.py` wrote the PLAY cap as `button_cap.stl`, which read like a
  generic part next to `cue_cap.stl`. It's now `play_cap.stl`.
- The light ring had no STL. It's now `light_ring.stl`: print two, in
  translucent PETG or resin.

## 2026-11-01: PTA4543 pitch pot mount and the control board (CAD only)

- Pitch pot chosen: **Bourns PTA4543-2015DPB103** (45 mm, no centre detent,
  15 mm metal lever, 10k linear). Its frame hangs on two 3 mm pads under the
  top, held by two M2 x 8 countersunk screws into its tapped holes (56 mm
  apart). The slot shrinks from 74 to 49.6 mm (travel + lever + 0.6),
  leaving 2 mm of plastic to each screw hole. The knob gets a 1.4 x 4.3 x
  7.2 mm lever socket, is 10 mm tall (was 9) and rides 1 mm above the top.
  The ticks now span the real 45 mm travel.
- New `ctrlboard.py`: one 40.5 x 56.5 mm board under CUE / PLAY, with three
  M2.5 screw bosses down from the top. Its top face is z 20.7 (angled) /
  24.7 (flat), where a 12 x 12 x 7.3 mm tact switch meets the cap's
  underside. Top side: switches and 4 x 0805 LEDs per light ring. Bottom
  side: the I2C chip and JST-XH for the pot (3), AS5600 (4) and dev board
  (5). It writes `build/ctrl_board_outline.dxf` and prints every position
  relative to the board corner.
- The Cherry-MX switch stand-ins are gone; the caps now rest on the tact
  switches.
- Clash check over every new part in both configs: clean (only the knob's
  inlaid index line overlaps, as intended). Shells watertight, one body.
- **Reprint needed:** the shell (both configs) and the slider knob.
- Renders 0028 (slot, countersinks) and 0029 (board section).

## 2026-11-01: Music plays out of the UCA202

- `usb_audio_probe` becomes `usb_audio`, a real output. When a UAC1 device
  with a 16-bit stereo isochronous output at 44.1 kHz enumerates, the deck
  streams to it in stereo. The speaker gets silence, and comes back when
  the device is unplugged.
- `audio_out::write()` feeds a 4096-frame ring. The USB side sends one
  packet per 1 ms frame, 44 or 45 frames, 4 packets per transfer, 3
  transfers in flight. It holds the ring near 1024 frames (~23 ms). Past
  ±512 frames it drops or repeats one frame per packet to follow drift
  between the I2S and USB clocks. The I2S DMA still paces the audio loop.
- Mute is done by sending silence to I2S, not by muting the codec. The
  first try muted the codec over I2C from the audio task (core 1), which
  collided with LVGL's touch read on the same bus. The touch read failed
  and the deck aborted at boot, over and over.
- The 2 s report adds `usb audio: underruns N  drift corrections N`.
- Budget (idle, no track loaded): uac_client 1 % of core 0, audio 9-10 %
  of core 1. Added latency on USB: ~23 ms ring + ~12 ms in flight.

## 2026-11-01: UCA202 enumerates; the probe tone plays with no errors

- The UCA202 (TI PCM2902, 08BB:2902) failed to enumerate: "Configuration
  descriptor larger than control transfer max length". The hub then
  switched off its port, so the UCA202's power LED stayed dark. It lists
  about 40 formats, which puts its config descriptor over 1 KB.
  `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` goes from 256 to 4096; 1024
  still failed. Each device gets a 4 KB control buffer, in PSRAM.
- Probe result through the hub: playback on interface 1 alt 1 (2 ch,
  16-bit, 32/44.1/48 kHz, isochronous OUT 0x02, adaptive, 192 bytes).
  Capture on interface 2 (IN 0x84, async). A 2 s tone at 48 kHz:
  1,000 packets, **0 packet errors, 0 transfer errors**.
- SET_CUR sample rate on the endpoint stalls. The tone still played, so
  the PCM2902 apparently follows the packet rate.

## 2026-10-31: USB audio probe (Behringer UCA202, not yet tested)

- New `usb_audio_probe`: its own USB host client. When a device with a USB
  audio streaming interface (UAC1) enumerates, it logs the audio
  interfaces, terminals, formats, rates and endpoints. It then plays a 2 s
  440 Hz tone at -12 dBFS through the first 16-bit stereo isochronous
  output that takes 48 kHz: 192-byte packets, 8 per transfer, 3 transfers
  in flight. It logs packet and transfer errors at the end.
- Purpose: confirm isochronous transfers work through the full-speed hub
  before building an `audio_out` backend on them. Nothing else uses it.
  The DJ2Go (MIDI only) is skipped.
- Budget: not measured. It's idle until an audio device is plugged in.
  App 0x162900 bytes (83 % of the partition free).

## 2026-10-31: AS5600 on four bosses, real board size (CAD only)

- Board from Dan's ruler and the listing photo: 15/16" (23.8 mm) square,
  four corner holes 5/8" (15.9 mm) apart, chip in the middle. Two pin
  columns either side of the chip: 3 pins on the left, 4 on the right.
- Four 2.3 mm bosses (7 mm OD, small M3 inserts) replace the two guessed
  ones. Shallow pockets under both pin columns take the solder tails; the
  right one notches the top-right boss (0.55 mm wall round its insert).
- Only the base plate changes. Clash check clean in both configs.

## 2026-10-31: encoder on bosses, no e-clip (CAD only)

- The e-clip is gone. The printed shaft now ends just under the lower
  bearing (tip at z 9.5), so the magnet sits 2.3 mm higher and the AS5600
  board goes up on two 2.3 mm bosses with small M3 inserts. The air gap
  stays 1.1 mm. The header's Dupont plug clears the bearing boss by 0.6 mm.
- Shaft below the platter: 27.0 mm flat, 23.0 mm angled.
- Nothing holds the platter down for now. The shell is unchanged; only
  the platter and base plate change.
- No firmware change, so the budget is unchanged.

## 2026-10-31: case engraving reads "FREEDJ" (CAD only)

- The hardware brand is freedj (freedj-esp stays the firmware's name), so the
  bezel engraving is "FREEDJ" instead of "FREEDJ-ESP". Nothing else changed.

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
