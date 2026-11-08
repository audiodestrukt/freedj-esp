# A custom main board (plan)

Status: plan only, nothing designed yet (2026-11-08). What a freedj-esp board of our own
would carry, so the deck isn't tied to dev kits. It's the board the
[configurations](CONFIGS.md) point to once the dev kits run out: a DAC and PSRAM on one board,
with pins that are free for the deck. Chips are compared in [TARGETS.md](TARGETS.md).
The controls board in [hardware/ctrl-pcb](hardware/ctrl-pcb/README.md) is separate and stays
separate (CUE, PLAY, pitch over I2C at 0x42).

## Why

Each board so far is missing something:

| Board | Missing |
|---|---|
| P4 dev kit | size and cost; its codec is a mono ES8311 driving a speaker |
| A1S Audio Kit | free pins: every header line is shared with a key, an LED or the amp enable, which capped the screen at 4 MHz; no USB host |
| NodeMCU-32S | a DAC and PSRAM |
| XIAO ESP32S3 | a DAC, SD and room for controls (fine as a keychain, see CONFIGS.md) |

## Two variants, one design language

| | **S3 board** (portable, first) | **P4 board** (full deck, later) |
|---|---|---|
| MCU | ESP32-S3 module with octal PSRAM, e.g. WROOM-1 **N16R8** (16 MB flash, 8 MB PSRAM) | ESP32-P4 module with 32 MB PSRAM |
| UI tiers | Minimal and Compact; Full on a 320×240 SPI screen | Full, up to MIPI-DSI |
| USB host | full speed (fine for the DJ2Go; a stick reads at ~1 MB/s) | high speed |
| Link | Wi-Fi only (see TARGETS.md) | Ethernet (needs a PHY and magjack) |
| Power | LiPo | mains or USB-C PD |

## Blocks

Part names are candidates to check, not choices. Stock-check each at JLCPCB/LCSC before
the layout (see the pcb-layout workflow: real parts imported, low stock swapped early).

| Block | Candidates | Notes |
|---|---|---|
| MCU | S3 WROOM-1 N16R8; P4 module | Module, not bare chip: antenna, flash and PSRAM done |
| Codec / DAC | **ES8388** (stereo in and out, the A1S's; driver and fixes already in `audio_out.cpp`); WM8960 (stereo, headphone driver built in); PCM5102A (DAC only, no input) | A codec with line in keeps timecode vinyl possible. The ES8311 is mono: not for headphones |
| Headphone out | the codec's own driver, or a small headphone amp | 3.5 mm jack with detect; the ES8388 auto-mute fix (1 LSB silence) carries over |
| Speaker (optional) | NS4150-class mono amp with an enable pin | Its enable gets a **dedicated** pin |
| Storage | microSD, SDMMC 4-bit, with card detect | 4-bit, not 1-bit: the A1S runs 1-bit |
| USB | USB-C: host/OTG for the DJ2Go and sticks, plus power and the console | S3: native USB on GPIO 19/20; switching host vs device needs VBUS control |
| Power (S3) | LiPo charger with power path, 3.3 V buck or LDO, battery gauge on an ADC pin | Audio rail kept quiet (separate LDO for the codec's analog side) |
| Screen | SPI header (ILI9341 / ST7789 / ST7735), plus I2C for an OLED | Short, dedicated lines at 40 MHz, with a ground beside SCK |
| Jog | AS5600 on I2C, or an optical encoder on PCNT | Header for each |
| Browse | EC11 on PCNT, plus its push | |
| Controls | connector for the ctrl-pcb (I2C 0x42, 5 wires) or plain buttons and a pot | Same 5-wire cable as today |
| Debug | boot and reset buttons, UART pads | |

## Pin rules (the A1S lessons)

- **Every deck signal gets its own pin.** No key, LED or amp enable on a screen or audio line. On the A1S, shared lines limited the screen to 4 MHz however it was wired.
- **Screen SPI on the chip's own SPI pins** where the chip has them (IOMUX), short traces, a ground next to SCK.
- **I2C buses split by job if the OLED is on I2C:** codec and touch on one, the OLED on another, so a 23 ms OLED frame can't delay anything else. The jog on PCNT avoids the question.
- **S3 pins to avoid:** the strapping pins (0, 3, 45, 46), GPIO 35–37 (taken by octal PSRAM), 19/20 (USB).
- **Spare pins to a header**, so a new control doesn't need a new board.

## Firmware side

- A `board_<name>.h` pin map and an `sdkconfig.defaults.<name>` fragment, as for the A1S.
- The S3 port (TARGETS.md) has to exist first; the board then only adds its pin map.
- Bring-up: the same colour sweep and the A1S checklist (codec registers, SD, screen speed, console UART).

## Open questions

- Codec: ES8388 (known, driver done) or WM8960 (headphone driver built in)? Both need a stock check.
- S3 board first, or straight to the P4 board?
- Battery size and runtime target for the S3 board.
- Does the board include the jog's mount (AS5600 under the platter), or stay a separate breakout on a cable?
- Screen: SPI only, or leave room for a parallel or RGB interface on the S3 (faster than SPI)?
- Enclosure: the existing cases ([hardware/](hardware/README.md)) or a new one sized to the board? Run the pcb-enclosure-fit loop once there's an outline.

## Out of scope

The smallest decks (PWM or bit-banged audio on a C6 or smaller) are a separate project,
**freedj-nano**, not started. See CONFIGS.md.
