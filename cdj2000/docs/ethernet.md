# The CDJ-2000's Ethernet, and Link on it

As the emulators model it (cdj-nxs2-qemu `hw/cdj/common/cdj_ether.c`) and the
Renesas SH7763 manual describes it; our own notes for our own driver
(`main/netstack.cpp`).

## The controller

- The SH7763's E-DMAC + EtherC at **0xFEF00000** (E-DMAC registers from
  0x000, EtherC from 0x100), with a PHY behind it (RTL8201CP on the board,
  reached by bit-banging MDIO through PIR at 0x120; we don't touch it yet,
  the PHY's power-on defaults negotiate).
- The E-DMAC walks rings of descriptors in memory. With EDMR.DL = 00 each is
  16 bytes: word 0 status (bit 31 TACT/RACT = the controller owns it, bit 30
  TDLE/RDLE = the last in the ring, bits 29/28 the frame position, 11 = a
  whole frame), word 1 the buffer length in its high half (RD1's low half is
  the received length), word 2 the buffer's **physical** address.
- EDMR.DE set: frames are not byte-swapped (the CPU is little-endian, and
  the descriptors are always in its order).
- MAHR/MALR hold our MAC address; the receiver takes broadcast, multicast
  and frames to that address.
- **PSR bit 0 (LMON) reads 0 when a cable is in**, on the SH7763 boards (the
  model notes the NXS2 is the other way round).
- ⚠ Registers the model leaves alone but real hardware may want set (FIFO
  sizes FDR, thresholds TFTR/FCFTR, RFLR) are left at reset values.

## Our stack

ARP (answers, a gratuitous announce at start, learns from every IPv4 frame),
IPv4 without fragments, UDP with checksums, six sockets. The address is
link-local, **169.254.x.y from the MAC** (what a player on a cable with no
DHCP server ends up with); ⚠ no clash probe or DHCP yet. ⚠ The MAC is made
up (02:cd:20:00:20:01) until the deck's own is read from its flash.

## Link

freedj-esp's own Pro DJ Link code (`p4/deck/main/prolink.cpp`), compiled
unchanged on `shim/` (ESP-IDF's FreeRTOS, esp_timer, esp_log and lwIP socket
headers over our stack) as a cooperative task beside the main loop
(`main/coop.cpp`). `tools/test_link.py` runs our image on a private QEMU
segment (multicast on loopback only) beside an XDJ-1000MK2 played from a
real one's packets, and checks the announce, the keep-alives, ARP, our
status to it, and that we follow it as the tempo master.

## Traps found on the way

- libgcc for this toolchain is position-independent: its routines call each
  other through the GOT, so the linker script has to keep `.got` in the
  image. Without it a 64-bit division returned garbage and a float-to-int64
  conversion jumped to address 0.
- GCC's SH-4A code switches the FPU's precision with `fpchg` from a
  double-precision default: FPSCR.PR must be 1 at entry (start.S sets it,
  and every cooperative task starts with it).
