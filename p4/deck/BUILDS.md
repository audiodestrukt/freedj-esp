# Build options (plan)

Status: plan only, nothing built yet (2026-11-01).

One enclosure design and one firmware image, built more than one way. The first release
uses **only off-the-shelf parts**: no custom PCB, no second microcontroller. The control
board in [hardware/ctrl-pcb](hardware/ctrl-pcb/README.md) stays as the later, tidier option.

## The options

| Part | Release 1: off the shelf | Later |
|---|---|---|
| Host | Waveshare ESP32-P4 dev kit | same (Pi Zero / Teensy hosts are a separate project) |
| CUE / PLAY | panel-mount pushbuttons with built-in LEDs, wired to P4 GPIO | control board: tact switches + ring LEDs |
| Pitch | 45 mm slide pot, wiper on a P4 ADC pin | same pot, read by the control board's CH32V003 |
| Jog | AS5600 breakout on I2C (works today) | same, through the control board's J_ENC |
| Touch | screen touch (0x38) on the same I2C bus as the AS5600 (0x36) | same |
| Platter | knurled or Pioneer (`FREEDJ_PLATTER`) | same |
| Case | angled or flat (`FREEDJ_CONFIG`) | same |

Release 1 parts list:
- the P4 dev kit and the MSP4021 screen;
- an AS5600 breakout, its magnet, two 608 bearings and the steel inertia ring;
- two panel-mount buttons;
- a 45 mm slide pot;
- printed parts, screws, and Dupont/JST jumpers.

## Firmware: one image, picks its inputs at boot

Every input already ends up as a `deck::Event`, so each backend just has to produce those
events. The DJ2Go over USB MIDI and the AS5600 do this already.

- **New `panel_gpio.cpp`** (direct wiring):
  - **Buttons:** internal pull-up, switch to GND. Scan at 1 kHz from the jog task's core-0
    loop, or use a GPIO interrupt plus a timer, with 5 ms debounce. Sends the same
    Play/Cue events the DJ2Go sends.
  - **Pitch:** `adc_oneshot` at about 200 Hz with 4× oversampling.
    - An IIR filter plus a deadband about ±2 LSB wide after filtering, so a fader at rest sends nothing.
    - End calibration (lo/hi), learned like the analog jog's, so pots that don't reach the rails still give the full range.
    - The P4 ADC wanders about ±40 counts. Filtered, that's roughly 10 clean bits, about
      0.02 % steps on ±10 %, which is finer than a CDJ's 0.05 %.
  - **LEDs:** LEDC PWM, one channel per button LED. This sets the brightness, matches the
    two colours, and gives the CDJ's blinking CUE and PLAY.
- **Selecting the backend at runtime,** the same way the AS5600 driver picks I2C or analog:
  1. At boot, probe I2C 0x42 for the control board.
  2. If it answers, use it. Otherwise start `panel_gpio`.
  3. One binary covers both builds, so there's no Kconfig fork to keep in step.
  4. The panel pins are only configured as inputs or LEDC, so they do no harm on a build
     that doesn't wire them.
- **Pins go in `board.h`** under a `panel` block.
  - Still needed: the header map. Which header pins can read analog (GPIO 16–23 or 49–54)
    and which are free.
  - Get it from Waveshare's pinout image or schematic, or run a scan on the board itself:
    drive each candidate pin and read it back.
  - Rough need: 2 button inputs + 1 ADC + 2 LED outputs = **5 pins**. There are 28
    programmable GPIOs on the headers, about 12 of them in use.

## CAD: a `FREEDJ_CONTROLS` switch

`FREEDJ_CONTROLS = "panel"` (release 1) or `"board"` (today's geometry).

- **`panel`:**
  - Plain round holes at PLAY_C and CUE_C, sized from new `PANEL_BTN` params:
    - hole diameter;
    - body depth below the top;
    - thread length;
    - nut and flange diameter, checked against the jog well and the edge.
  - Locally thin the 4 mm top plate where needed, so the nut catches the thread.
  - No light rings, caps, ctrl standoffs or bosses.
  - The fader keeps its pads, unchanged.
  - The clash check takes a simple body for each button (a cylinder at the given depth),
    so it shows what the button hits inside.
- **`board`:** today's geometry. `FREEDJ_BUTTONS` lit/plain applies only here.
- **Depth is the risk.** The angled case is 34 mm tall, and many lit 16–19 mm buttons are
  25–35 mm deep behind the panel.
  - Check the chosen button against the base and the dev board at its position.
  - Choose low-profile ones, or put them in the flat case (38 mm) if needed.

## Wiring (release 1)

There's no harness board, just jumpers from the front-left controls to the dev board's headers.

- **Buttons:** 2 signal wires plus a shared GND.
- **LEDs:** 2 signal wires (the resistor goes on the button lead, or the button has one built in) plus a shared GND.
- **Pot:** 3V3, wiper, GND.
- **AS5600:** 4 wires (3V3, GND, SDA, SCL), with DIR tied to GND at the breakout.

Total: about 11 conductors. A wiring table goes in hardware/README.md once the pins are chosen.

## Order of work

1. **Choose the buttons.**
   - Need: 3.3 V or 5 V LED (not 12 V), and momentary.
   - Get the hole diameter and the depth behind the panel.
2. **Get the P4 header map and choose the pins:** 1 ADC pin, 4 GPIO.
3. **Firmware:**
   - `panel_gpio` (buttons, then pot, then LEDs);
   - the 0x42 probe with fallback;
   - a serial `p` key for the panel's stats, like `e` for the jog.
4. **Get touch back:** screen touch and the AS5600 on one bus (TODO.md).
5. **CAD:**
   - add the `FREEDJ_CONTROLS=panel` holes and the depth check;
   - print the top plate only, to test the fit.
6. **Docs:**
   - the release 1 BOM and wiring table in hardware/README.md;
   - this file, updated as each step lands.

## Open questions

- Which buttons? Size and LED voltage decide the hole and how deep the button sits.
- If the CUE and PLAY LEDs are buttons with built-in LEDs, the light-ring look goes away
  in release 1. Is that OK, or should the rings stay as wired ring LEDs (8 pins, or 2
  pins and 2 transistors)?
- Does the control board stay on the roadmap as a "deluxe" build, or does release 1's
  wiring become the only build until there's a full custom main board?
