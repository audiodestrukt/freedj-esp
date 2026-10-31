# freedj-esp: potential hardware targets

Which ESP32 chips and boards freedj-esp could run on, compared against what it
actually uses today. Written 2026-10-31. Prices move fast and some are
unconfirmed, so check before buying. Chip specs for the S31 come from a
datasheet still marked preliminary.

## What freedj-esp needs (measured on the current board)

From the [README's resource budget](README.md#resource-budget) and the
[changelog](CHANGELOG.md):

| Need | Why | Measured on the P4 |
|---|---|---|
| **Two cores**, one kept for audio | 5.8 ms audio blocks with keylock, next to the screen, USB and network | Core 1 (audio): 9–12 % varispeed, ~28 % keylock. Core 0: 40–60 % |
| **Internal RAM** | Task stacks, DMA buffers, LVGL's per-frame work | 77–80 KB free at the lowest point. **The tightest resource** |
| **PSRAM, large and fast** | 12 MB track window (~70 s), peaks, waveform canvases, track list | ~13 MB used of 32 MB. Bandwidth matters: screen traffic through PSRAM measurably slows keylock |
| **USB host** | DJ2Go (full speed) and the USB stick | Root port forced to full speed so the DJ2Go works behind the hub (ESP-IDF has no transaction translator) |
| **Ethernet MAC** | Pro DJ Link, wired as Pioneer gear expects | **100 Mbit** (IP101GRI PHY; log: "working in 100Mbps"), and plenty: the Link task uses 2–4 % of core 0, and even streaming a 320 kbps track to another player would be ~0.3 % of the link. Gigabit buys nothing here |
| **I2S** | DAC out (and a timecode ADC later) | |
| **A display interface** | SPI today (the MSP4021 stand-in); MIPI-DSI for the planned XDJ-style screen | |
| **Flash** | App 1.4 MB | 8 MB app partition |

## Candidates

### ESP32-P4: current target, recommended

Dual-core RISC-V at 360 MHz, 768 KB internal RAM, 32 MB hex PSRAM (in the
P4NRW32 variant), USB 2.0 high-speed OTG, 100 Mbit Ethernet MAC, MIPI-DSI.
Everything freedj-esp does today runs with headroom: about 70 % of the audio
core is free, about 40 % of core 0, and 19 MB of PSRAM. No Wi-Fi on the chip
(boards pair it with an ESP32-C6).

Boards, cheapest first (prices seen 2026-10-31):

| Board | Price | Notes |
|---|---|---|
| Bare ESP32-P4 module (P4NRW32) on our own PCB | about $4.50–6 in small quantities, per [one buying guide](https://electricalflux.com/mcu-boards/esp32-p4-arduino-ide-suitability-guide) (not confirmed with a distributor) | For a finished product |
| [Olimex ESP32-P4-DevKit](https://www.olimex.com/Products/IoT/ESP32-P4/ESP32-P4-DevKit/open-source-hardware) | about €16 | P4NRW32 (32 MB PSRAM), 16 MB flash, Ethernet; open-source hardware. No audio codec as far as I can tell, so add the planned PCM5102A. Check its USB host ports for the DJ2Go plus stick |
| [Waveshare ESP32-P4-ETH](https://www.waveshare.com/product/esp32-related/boards-kits/esp32-p4.htm) | $12.99–74.99 by option; the 32 MB variant is about $22.58 on [AliExpress](https://www.aliexpress.us/item/3256809018130584.html?gatewayAdapt=deu2usa4itemAdapt) ([Newegg](https://www.newegg.com/p/3D0-002E-001A0): $49.48) | Ethernet and a speaker header, so probably closest to the current board. Check the variant has 32 MB PSRAM |
| Waveshare ESP32-P4-Module-DEV-KIT | | **Current development board.** 4× USB-A via a CH334F hub (3 usable with the hub on, jumper H3 on DEVICE), ES8311 codec, Ethernet, DSI connector |

Porting cost: none. Watch the PSRAM size on cheaper boards: the 16 MB
variants would need a smaller track window.

### ESP32-S31: the closest alternative; tighter, no DSI

Dual-core RISC-V at 320 MHz (one core with SIMD), 512 KB internal RAM, 16 or
32 MB **octal** PSRAM in-package, USB 2.0 high-speed OTG, Gigabit Ethernet
MAC, Wi-Fi 6 and BLE 5.4 on chip. In mass production since July 2026
([Espressif](https://www.espressif.com/en/news/ESP32_S31_Mass_Production),
[datasheet](https://documentation.espressif.com/esp32-s31_datasheet_en.html),
preliminary).

| For freedj-esp | |
|---|---|
| Good | Same CPU family as the P4, so it's mostly a re-target, not a rewrite. Ethernet and USB host on chip. Wi-Fi built in. (Its MAC is gigabit, but freedj-esp has no use for more than 100 Mbit) |
| CPU | ~10 % slower clock; probably fine given the headroom |
| **Internal RAM** | **A third less (512 vs 768 KB).** Our tightest resource, so it would need a trimming pass first (move more to PSRAM) |
| **PSRAM bandwidth** | Octal (8-bit) vs the P4's hex (16-bit) at 200 MHz: likely about half. Screen-versus-keylock contention is already measurable on the P4, so whether waveform and keylock still fit together needs measuring |
| USB | Same missing hub translator: reported on the S31 in [espressif/esp-idf#17245](https://github.com/espressif/esp-idf/issues/17245). Our full-speed workaround should carry over |
| **Display** | **No MIPI-DSI** ([single source](https://esp32.co.uk/esp32-s31-vs-esp32-s3-is-s31-the-new-s3/); the datasheet doesn't list it). SPI or parallel RGB only, which rules out the planned DSI screen |
| Price | Board prices not found yet ([XDA](https://www.xda-developers.com/newest-esp32-run-linux-close-to-raspberry-pi/), [teachmemicro](https://www.teachmemicro.com/esp32-s31-explained-specs-risc-v-wi-fi-6-and-esp32-s3-comparison/) cover specs only) |

Worth revisiting as a cheaper single-chip build with an RGB screen, if boards
come in clearly under P4 prices.

### ESP32-S3: possible, as a cut-down port

Dual-core Xtensa LX7 at 240 MHz, 512 KB internal RAM, 8–16 MB PSRAM on
common modules, USB OTG full speed only, no Ethernet MAC, no DSI. N16R8
modules retail for a few dollars.

| For freedj-esp | |
|---|---|
| CPU | Different architecture (Xtensa), so a port, not a rebuild. Keylock plus waveform would probably be tight; unmeasured |
| PSRAM | 8 MB means a track window of about 20–40 s instead of 70 |
| Ethernet | Needs an SPI chip (W5500) for Pro DJ Link, costing CPU |
| USB | Full speed only. That's what we run anyway behind the hub, so no loss in practice |
| Display | SPI or parallel RGB; no DSI |

Probably the cheapest thing that could run a reduced freedj-esp: a smaller
buffer and maybe no keylock with the waveform.

### Classic ESP32 (the ESP32-A1S build): no

No USB host, 4–8 MB PSRAM, not enough CPU for this UI. It stays the
timecode-vinyl build in the repo root.

## Recommendation

Stay on the **ESP32-P4**. It's the only option where everything already built
fits with headroom, and the Olimex board makes it about €16 plus a DAC. Keep
the **S31** in mind for a cheaper RGB-screen version once board prices are
known, after measuring internal RAM and PSRAM bandwidth on it.
