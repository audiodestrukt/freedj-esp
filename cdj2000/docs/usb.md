# The CDJ-2000's USB host, and the stick on it

As the emulators model it (cdj-nxs2-qemu `hw/cdj/common/usb_r8a66597.c`) and
Renesas's manuals describe the module (the SH7724/SH7763 "USB 2.0
host/function module", an R8A66597-family controller). Our own notes for our
own driver (`main/usb.cpp`).

## The controller

- At **0xFE400000**, one root port. Host mode: SYSCFG DCFM, pull-downs
  (DRPD), the clock (SCKE) and the module (USBE) on.
- A device arriving raises INTSTS1 **BCHG** (enabled in INTENB1); a bus reset
  is DVSTCTR0 **USBRST**, then UACT for SOFs.
- **Control transfers** on the DCP (pipe 0): the setup packet comes from
  USBREQ (bRequest << 8 | bmRequestType), USBVAL, USBINDX, USBLENG and goes
  out on DCPCTR **SUREQ**; INTSTS1 SACK (acknowledged) or SIGN (not). The
  data stage: DCPCFG DIR for its direction, PID = BUF in DCPCTR, the bytes
  through **CFIFO** (CFIFOSEL names the pipe; ISEL to write the DCP), BRDY
  or BEMP per pipe. The status stage: DCPCTR **CCPL** with PID = BUF, the
  opposite direction. DCPMAXP holds the device address (DEVSEL, bits 15:12)
  and the packet size.
- **Bulk** on pipes 1 (OUT) and 2 (IN): PIPESEL then PIPECFG (type, CNTMD,
  SHTNAK, DIR, endpoint), PIPEBUF, PIPEMAXP (DEVSEL, packet size); SQCLR in
  PIPEnCTR for DATA0. OUT: write CFIFO, BVAL, PID = BUF, wait BEMP. IN: a
  transaction count (PIPEnTRE TRENB, PIPEnTRN) so the controller stops at the
  end of a reply, PID = BUF, wait BRDY, read CFIFOCTR's length, read CFIFO.
- The stock firmware moves sector data with the DMAC (D0FIFO); ours reads
  CFIFO by hand, **1 KB (two sectors) per command**, one pipe buffer's worth.
  Slow, but the same transfers the hardware does. ⚠ Faster reads (bigger
  transfers, the DMAC) come when audio needs them.

## Mass storage

Bulk-only transport (31-byte CBW, 13-byte CSW) with SCSI TEST UNIT READY,
READ CAPACITY(10) and READ(10). 512-byte blocks only.

## Files

`main/fat.cpp`: FAT12/16/32, read-only, long names (UCS-2 folded to ASCII,
others as '?'), case-insensitive lookup, a partition table or none.
`main/files.cpp`: stdio's `fopen`/`fread`/`fseek`/`fclose` and dirent's
`opendir`/`readdir` on it, the stick at **/usb**, so freedj-esp's
`rekordbox.cpp` (the analysis files: track paths and beat grids) builds
unchanged. `main/runtime.cpp` gives the C++ library a heap.

`tools/test_usb.py` builds its own stick image (`tools/fatimage.py`, written
from the FAT layout, not by the code under test) and checks the whole chain
on the emulator.
