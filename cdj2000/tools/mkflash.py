#!/usr/bin/env python3
"""The display board's flash (2 MB, as the ADSP-BF531 sees it at 0x20000000):
erased, our program as an LDR boot stream at +0x10000 (where the boot reads
it; the first 64 KB are the boot loader's), the splash frame at +1 MB.

LDR blocks (ADSP-BF53x boot stream): u32 destination, u32 byte count, u16
flags (0x0001 zero-fill: no payload, 0x8000 the last block), then the bytes.
The core starts at 0xFFA08000 after the last one.

  python3 tools/mkflash.py <gui.elf> <splash.rgb555> <out.flash>
"""
import struct, sys

SIZE, APP, SPLASH = 2 << 20, 0x10000, 0x100000
PT_LOAD = 1


def segments(path):
    """(address, file bytes, memory size) for each loadable part of an ELF32 (little-endian)."""
    e = open(path, "rb").read()
    assert e[:4] == b"\x7fELF" and e[4] == 1 and e[5] == 1, "not a little-endian ELF32"
    phoff, = struct.unpack_from("<I", e, 28)
    phentsize, phnum = struct.unpack_from("<HH", e, 42)
    out = []
    for i in range(phnum):
        p_type, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", e, phoff + i * phentsize)
        if p_type == PT_LOAD and memsz:
            out.append((paddr, e[off:off + filesz], memsz))
    return out


def ldr(segs):
    blocks = []
    for addr, data, memsz in segs:
        if data:
            blocks.append((addr, data, 0))
        if memsz > len(data):
            blocks.append((addr + len(data), b"", 1, memsz - len(data)))
    out = b""
    for i, b in enumerate(blocks):
        last = 0x8000 if i == len(blocks) - 1 else 0
        if b[2] == 1:
            out += struct.pack("<IIH", b[0], b[3], 1 | last)
        else:
            out += struct.pack("<IIH", b[0], len(b[1]), last) + b[1]
    return out


if __name__ == "__main__":
    elf, splash, outp = sys.argv[1:4]
    img = bytearray(b"\xff" * SIZE)
    stream = ldr(segments(elf))
    assert APP + len(stream) <= SPLASH, "program too big"
    img[APP:APP + len(stream)] = stream
    pix = open(splash, "rb").read()
    img[SPLASH:SPLASH + len(pix)] = pix
    open(outp, "wb").write(img)
    print(f"{outp}: LDR {len(stream)} bytes at 0x{APP:x}, splash {len(pix)} bytes at 0x{SPLASH:x}")
