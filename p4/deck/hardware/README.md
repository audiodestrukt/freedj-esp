# ESP Deck hardware: a small 3D-printed CDJ

A scaled-down CDJ around a **3.5" 480×320 LCD**: the CDJ layout with only a jog
wheel, CUE / PLAY and a pitch slider as physical controls, and the angled screen
bezel at the top. Everything else is touch, on the screen. First design,
2026-10-31; nothing printed yet.

![ESP Deck, first design](cad/renders/esp_deck/0001-first-design-hero.jpg)

| | |
|---|---|
| Footprint | **160 × 195 mm**, 34 mm tall at the front, 48 mm at the back of the bezel. The top shell fits a 256 mm bed in one piece |
| Screen | 3.5" 480×320, **ST7796S** over SPI (the same resolution and driver as the 4" MSP4021 now, so the UI carries over). Avoid ILI9488 panels: over SPI they only take 18-bit colour, about a third fewer fps. Bezel tilted 12° |
| Jog | 100 mm printed platter, knurled rim, on an **8 mm shaft through two 608 skateboard bearings** pressed into a boss under the top. Steel inertia ring (r 38–46 mm, 5 mm) in the platter's underside. Aluminium top plate, which doubles as the capacitive touch electrode |
| Jog sensing | 6×2.5 mm diametric magnet in the shaft end, 0.9 mm over an **MT6701** on a 22 mm board. A/B quadrature into the P4's pulse counter: no CPU, off the shared I2C bus |
| CUE / PLAY | 19 mm caps in translucent light rings (amber, green), bottom left as on a CDJ, over key switches |
| Pitch | 45 mm slide pot (Bourns PTA4543 class), knob at the right, tick scale, green tempo-zero LED |
| Top | USB-A stick slot left of the screen with an activity LED, "ESP DECK" engraved |
| Rear | RJ45 (Pro DJ Link), RCA L/R line out (PCM5102A), USB-C power |
| Assembly | The base plate screws into four corner posts (M3 countersunk into heat-set inserts). Rubber feet |

![Section through the jog](cad/renders/esp_deck/0003-first-design-jog-section.jpg)

## Open questions

- **Does the electronics fit?** Not checked yet: the Waveshare dev board has to fit
  inside 155 × 190 × ~28 mm with its Ethernet, USB and audio reaching the back.
  Measure it before printing the shell. If it doesn't fit, grow the shell or use
  the Olimex board or our own carrier board ([TARGETS.md](../TARGETS.md)).
- **Jog top touch:** capacitive through the aluminium plate (first choice), or a
  floating platter on a microswitch. Capacitive needs a P4 touch-capable pin free.
  This is what lets the deck scratch while playing, not only when paused.
- **Magnet on a steel shaft** weakens and bends the field. If the readings are
  noisy, use a brass or aluminium shaft, or a printed shaft end.
- **Air gap:** 0.9 mm in the model; check against the MT6701 datasheet when
  the board is drawn.
- **Firmware:** a native input driver (pulse counter, ADC, GPIO, touch) that feeds
  the same `DeckEvent`s the DJ2Go does today.

## Suggested build order

1. The jog module alone, wired to the dev board. Feel is the product and the riskiest part.
2. Slider and buttons on a test plate.
3. The shell and base, once the board fit is measured.

## CAD

build123d, parametric. Every shared dimension is in `cad/machine_params.py`, and
parts are placed by named mate points, so changing a number moves everything
that depends on it.

| File | What |
|---|---|
| `machine_params.py` | All shared dimensions, and `place()` |
| `shell.py` | Top shell: jog bearing boss, button holes, pitch slot, bezel, rear cutouts |
| `base.py` | Base plate and feet |
| `jog.py` | Platter, touch plate, centre cap, inertia ring, bearings, shaft, magnet, encoder board |
| `controls.py` | Button caps and light rings, slider knob, pot, LEDs |
| `stock.py` | Bought parts as stand-ins: LCD, cover glass, connectors |
| `machine.py` | The assembly, exported as GLB (plus a cutaway through the jog) |
| `screen_mock.py` | A picture of the play screen for the render, laid out from `main/ui.cpp`. The waveform is synthetic |
| `deck_render.py` | Headless Blender render (Cycles, GPU) |

```bash
cd p4/deck/hardware/cad
python3 shell.py; python3 base.py; python3 jog.py; python3 controls.py   # printable STLs in build/, each checked watertight
python3 screen_mock.py && python3 machine.py                            # build/esp_deck.glb, build/esp_deck_cut.glb
LC_ALL=C LANG=C blender -b -P deck_render.py -- build/esp_deck.glb build/hero.png --dist 3.4
LC_ALL=C LANG=C blender -b -P deck_render.py -- build/esp_deck_cut.glb build/cut.png \
    --az 90 --el 14 --dist 2.0 --target 0.08,0.07,0.022 --exposure -0.3
```

Renders worth keeping go in `cad/renders/esp_deck/` with a line in its `INDEX.md`.
