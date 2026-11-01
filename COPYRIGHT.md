# Copyright and licences

Copyright (c) 2026 Dan Newcome

freedj-esp is two kinds of work under two licences:

| What | Where | Licence |
|---|---|---|
| **Software**: the A1S firmware, the P4 deck firmware, host simulation, tests and tools, and their docs | everything except `p4/deck/hardware/` | **GPL-3.0-or-later**: [`LICENSE`](LICENSE) |
| **Hardware**: the enclosure CAD sources (build123d), the STL/DXF/GLB files they generate, the renders and the hardware docs | `p4/deck/hardware/` | **CERN-OHL-S-2.0**: [`p4/deck/hardware/LICENSE`](p4/deck/hardware/LICENSE) |

Both are reciprocal licences. If you distribute modified firmware, or make
and ship a modified deck, you have to publish your changes under the same
licence.

**Source location** (for CERN-OHL-S section 4):
<https://github.com/audiodestrukt/freedj-esp>

## Software notice

    freedj-esp
    Copyright (c) 2026 Dan Newcome

    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
    more details.

## Hardware notice

    freedj hardware (p4/deck/hardware)
    Copyright (c) 2026 Dan Newcome

    This source describes Open Hardware and is licensed under the CERN-OHL-S v2.

    You may redistribute and modify this source and make products using it
    under the terms of the CERN-OHL-S v2 (https://ohwr.org/cern_ohl_s_v2.txt).

    This source is distributed WITHOUT ANY EXPRESS OR IMPLIED WARRANTY,
    INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY AND FITNESS FOR A
    PARTICULAR PURPOSE. Please see the CERN-OHL-S v2 for applicable conditions.

    Source location: https://github.com/audiodestrukt/freedj-esp

    As per CERN-OHL-S v2 section 4, should you produce hardware based on this
    source, you must where practicable maintain the Source Location visible on
    the external case of the product or in its documentation.

## Third-party code

Bundled in this repo:

| Code | Where | Licence |
|---|---|---|
| minimp3 (lieff) | `p4/deck/main/third_party/minimp3` | CC0-1.0 |

Fetched at build time (not in this repo), under their own licences:

| Code | Used by | Licence |
|---|---|---|
| arduino-audio-tools, arduino-audio-driver (pschatzmann) | A1S firmware | GPL-3.0 |
| arduino-libhelix (pschatzmann), with the Helix MP3/AAC decoder | A1S firmware | GPL-3.0; the Helix decoder is RealNetworks RPSL/RCSL |
| U8g2 (olikraus) | A1S firmware, simulator | BSD-2-Clause |
| ESP-IDF and Espressif components (esp_codec_dev, esp_lcd_st7796, esp_lcd_touch_ft5x06, esp_lvgl_port, usb_host_msc) | P4 firmware | Apache-2.0 |
| LVGL | P4 firmware | MIT |

The A1S firmware links GPL-3.0 libraries, so its binaries are GPL-3.0 as
well. The timecode decoder is a clean-room implementation of the algorithm
published by xwax (GPL-3.0). No xwax code is included; see `src/timecode.h`.

## Trademarks

### The freedj name

"freedj", "freedj-esp", the FREEDJ engraving and the audiodestrukt name are
Dan Newcome's trademarks. The licences above cover the source and the designs.
**They don't grant any right to these names or logos.**

- You can build, modify and share the firmware and hardware under those licences.
- You can say truthfully that your work is "based on freedj".
- If you distribute a modified version, or sell decks or kits, give it a name of your
  own, so nobody takes it for the original.

### Other people's names

Pioneer DJ, AlphaTheta, CDJ, XDJ, DJM, rekordbox, Pro DJ Link, Serato, Numark,
Behringer and other product names are trademarks of their owners.

- freedj isn't affiliated with, endorsed by or sponsored by any of them.
- The names are used only to describe compatibility. "Pioneer-style" describes the
  look of a part and isn't a claim of origin.
