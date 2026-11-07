# freedj-esp: potential hardware targets

Which ESP32 chips and boards freedj-esp could run on, compared against what it
actually uses today. Written 2026-10-31; classic ESP32, S3, cheapest board and Link over Wi-Fi updated 2026-11-07 after the deck ran on the A1S. Prices move fast and some are
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

### ESP32-S3: the cheapest board worth porting to (not built yet)

Dual-core Xtensa LX7 at 240 MHz with vector (PIE) instructions, 512 KB internal
RAM, 8–16 MB **octal** PSRAM on common modules (all of it mapped), USB OTG full
speed, no Ethernet MAC, no DSI. N16R8 devkits are roughly $6–8 (from memory, not
checked with a seller).

| For freedj-esp | |
|---|---|
| CPU | Same family as the classic ESP32 that now runs the deck (below), one generation newer. The A1S's keylock (57–59 % of the audio core, worst blocks over budget) should fit with room, and WSOLA's correlation loop could use the vector unit. Unmeasured |
| PSRAM | 8 MB, all usable, and faster than the classic ESP32's quad 4 MB: a track window of about 30–40 s, against 12 s on the A1S and 70 s on the P4 |
| USB | **Host on chip**, full speed: the DJ2Go and a USB stick plug straight in (the classic ESP32 needs `tools/midi_bridge.py`). Full speed is what the P4 runs behind its hub anyway |
| Ethernet | None. Pro DJ Link would be Wi-Fi only, with the risks under *Pro DJ Link without Ethernet*; a W5500 over SPI is the wired option |
| Pins | A bare devkit has plenty: SPI screen, I2S DAC, SD and buttons without sharing pins, unlike the Audio Kit |
| Display | SPI or parallel RGB; no DSI |
| Porting cost | A third IDF target, like the A1S's: a pin map, a codec (a PCM5102A needs no I2C setup), and the USB code back in. About the size of the A1S port |

### Classic ESP32 (the ESP32-A1S build): runs, cut down

The A1S module is an ESP32-D0WD rev v3.0: 2 x Xtensa LX6 at 240 MHz, 520 KB SRAM, 8 MB PSRAM of which 4 MB is mapped, 4 MB flash.

Written off on 2026-10-31 as not enough CPU for this UI. On 2026-11-07 the deck ran on it
(README, *The ESP32-A1S build*), playing from SD with the 2.8" screen and the DJ2Go through the
serial bridge. Measured with `house.mp3`, waveform up:

| | Classic ESP32 (A1S) | P4 |
|---|---|---|
| Audio core, varispeed | 13–14 % | 9 % |
| Audio core, keylock | 57–59 %; worst block 6.2–10.2 ms of 5.8 | 27–28 %; worst 3.4 ms |
| MP3 decode (minimp3) | 17–18 % of core 0 | 8 % |
| Internal RAM free (lowest) | 94 KB (82) | 102–106 KB (80) |
| Track window | 2 MB, 12 s | 12 MB, 70 s |

- **Varispeed is comfortable.**
- **Keylock overruns its blocks.** The ~33 ms DMA queue covers it today, with no underruns, but a longer spike would drop out.
- **The A1S's screen runs at only 2 MHz.** That's the wiring on the Audio Kit, not the chip, so its UI load isn't comparable.
- **Missing:** USB host (so the DJ2Go goes through `tools/midi_bridge.py`), Ethernet, and timecode vinyl (its lookup table wants the whole 4 MB).
- **Bare classic-ESP32 WROVER devkits** cost about $5–6 (from memory) and have more free pins than the Audio Kit.

## The cheapest board that runs it

What the deck needs, from the A1S run:
- **Two cores,** one kept for audio.
- **At least 4 MB of PSRAM,** for the track window, the waveform peaks and LVGL.
- **About 300 KB of internal RAM.**
- **I2S** for the DAC, **SPI** for the screen, and **SD.**

Single-core chips are out: the C3, C5, C6 and H2, and the S2 too, though varispeed alone might fit on it.

| Board | Roughly (unchecked) | Verdict |
|---|---|---|
| Classic ESP32 WROVER devkit + PCM5102A DAC | $5–6 + $2–3 | **Cheapest that runs it.** The A1S proves the chip, with the limits above |
| ESP32-S3 N16R8 devkit + PCM5102A | $6–8 + $2–3 | **The one to buy.** A dollar or two more than the classic ESP32 for keylock headroom, twice the PSRAM, a USB host and free pins. Needs a port |
| AI-Thinker A1S Audio Kit | ~$12–15 | Codec and jacks included, but the board's own parts use up the pins (the screen tops out at 2 MHz on it so far) |

The screen ($8–12) and the AS5600 ($2) cost the same everywhere. A deck's electronics come to roughly $20–25 on an S3, against about $50 on the P4 route.

## Pro DJ Link without Ethernet

The classic ESP32 has an Ethernet MAC, but on the Audio Kit the PHY's pins are taken by the codec (GPIO 0, 25–27), and there's no room for a W5500 either. Neither the S3 nor the A1S can be wired, so Link would go over Wi-Fi, through a router that bridges Wi-Fi to the players' Ethernet.

- **Fine:**
  - The CPU: the Link task is 2–4 % of a core on the P4.
  - `prolink.cpp` is plain UDP sockets; only `net.cpp` needs a Wi-Fi version.
- **Risks:**
  - **Internal RAM.** Wi-Fi and lwIP want 50–70 KB against the A1S's 94 KB free. Their buffers can go to PSRAM.
  - **Beat-packet timing.** Beat packets are broadcasts. A router holds broadcasts back, by 100 ms or more, whenever any station on it uses power save. That needs power save off and DTIM 1, and it's the thing to measure: the phase loop smooths over about 0.3 s, which covers a few ms of jitter, not 100.
  - **Noise.** Wi-Fi transmit bursts can reach the audio on the Audio Kit.
- **Plausible:** following the tempo master (SYNC). Being master, browsing other players' media and serving our own all want wired timing or more RAM.
- **Test:** turn Link on over Wi-Fi on the A1S, follow a CDJ, and read the report's sync phase error (avg/max ms) as on the P4.

## Recommendation

- **The P4** stays the full deck: Ethernet Link, USB host, 70 s of buffer and headroom everywhere. The Olimex board makes it about €16 plus a DAC.
- **For a cheap deck, port to an ESP32-S3 N16R8** rather than building on the classic ESP32. It costs about the same and lifts the limits the A1S hit: keylock headroom, PSRAM, the USB host and pins.
- **The classic ESP32 / A1S build** stays as the proof that the deck runs cut down, and as a test bed with the bridge.
- **Keep the S31** in mind for an RGB-screen version once board prices are known, after measuring its internal RAM and PSRAM bandwidth.
