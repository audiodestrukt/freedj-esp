# The CDJ-2000 front panel link

What the main board (SH7763) and the front panel's microcontroller say to
each other, as the emulators model it (cdj-nxs2-qemu `hw/cdj/boards/cdj2000/panel.c`,
`hw/cdj/common/cdj_pnl_link.c`, its app face `app/faces/cdj2000.json`) —
written here in our own words, for our firmware. ⚠ marks what the models
only infer, or take from the CDJ-2000NXS2's panel.

## Wire

- SCIF2 of the SH7763, at **0xFFE20000**, in clocked synchronous mode. The
  stock setup: SCSMR = 0x80 (synchronous), SCBRR = 12, SCFCR = 0x06 (reset
  both FIFOs) then 0x30. The stock firmware moves the bytes with DMA; ours
  polls (the same registers, so the same wire).
- The main board drives each exchange, full duplex, **24 bytes** each way.
  The panel never speaks unasked.
- Each frame: **22 payload bytes**, then a checksum, then a sync byte
  **0x8F**. The checksum is an 8-bit sum of the payload with end-around carry
  (a carry out of bit 7 is added back in). ⚠ The models check only the
  panel's reply; we send the main board's frame in the same shape.

## The panel's reply (payload offsets)

Keys are bits, **set while held**; an idle panel's payload is all zero except
the DIRECTION lever and the slider.

| Byte | Bits |
|---|---|
| 0x04–0x05 | tempo slider position, big-endian (0x80xx = middle) |
| 0x06–0x07 | the slider's centre reference (0x80xx); position against it is the tempo |
| 0x08–0x09 | jog: a rolling 16-bit position counter, big-endian ⚠ (NXS2-confirmed, shared model) |
| 0x0A–0x0B | jog: pulse period P, big-endian; platter speed % = 27778 / P, 0 = still ⚠ |
| 0x0E | select knob: a rolling counter |
| 0x0F | 0x02 DIRECTION lever (**set = FWD**), 0x20 jog touched ⚠, 0x40 jog turning forward ⚠, 0x80 jog moving ⚠ |
| 0x10 | 0x01 PLAY/PAUSE, 0x02 CUE, 0x04 RELOOP/EXIT, 0x08 LOOP OUT, 0x10 LOOP IN, 0x20/0x40/0x80 HOT CUE A/B/C |
| 0x11 | 0x01 select knob push, 0x02 4-BEAT LOOP |
| 0x12 | 0x01 HOT CUE REC, 0x02 TRACK −, 0x04 TRACK +, 0x08 SEARCH −, 0x10 SEARCH +, 0x40 JOG MODE, 0x80 TEMPO RANGE |
| 0x13 | 0x01 LINK, 0x02 USB, 0x04 SD, 0x08 DISC, 0x10 TIME MODE, 0x40 MASTER TEMPO, 0x80 TEMPO RESET |
| 0x14 | 0x01 BROWSE, 0x02 TAG LIST, 0x04 INFO, 0x08 MENU/UTILITY, 0x10 BACK, 0x20 TAG TRACK |
| 0x15 | 0x01 CALL ½×, 0x02 CALL 2×, 0x04 DELETE, 0x08 MEMORY, 0x10 DISC EJECT |

## The main board's frame: lamps

Lamp bits by payload byte (from the app face, which lights its keys from
this frame); the rest of the frame is not mapped yet.

| Byte | Bits |
|---|---|
| 0x01 | 0x02 LOOP IN, 0x04 LOOP OUT, 0x10 CUE, 0x20 PLAY/PAUSE, 0x40 RELOOP/EXIT, 0x80 TEMPO RESET |
| 0x02 | 0x20 MASTER TEMPO |
| 0x03 | 0x02 DISC, 0x08 SD, 0x80 LINK |

## Testing against the emulator

cdj-nxs2-qemu reads key presses from a UDP socket (`CDJ_PANEL_KEYSOCK=<port>`,
datagrams `"<off>:<mask>:<ms>"` press for ms, `"<off>:<val>:0:lvl"` hold a
byte) and sends each changed main-board frame to `CDJ_PANEL_STATESOCK`
(`"pnl <hex>"`). `tools/test_panel.py` drives both.
