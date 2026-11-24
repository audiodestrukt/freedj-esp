#!/usr/bin/env python3
"""A FAT16 stick image for the tests, written from scratch: an MBR with one
FAT16 partition (type 0x06) at sector 63, 2 KB clusters, long file names.
Independent of the firmware's FAT reader, so it checks it.

  make_image(path, {"/dir/file name.ext": bytes, ...}, size_mb=32)
"""
import struct

SECTOR, SPC, RESERVED, NFATS, ROOT_ENTRIES, PART_LBA = 512, 4, 1, 2, 512, 63


def _short_names():
    n = [0]
    def next_name(long):
        n[0] += 1
        base = "".join(c for c in long.upper() if c.isalnum())[:6] or "F"
        ext = long.rsplit(".", 1)[1].upper()[:3] if "." in long else ""
        return f"{base}~{n[0] % 10}".ljust(8)[:8].encode() + ext.ljust(3).encode()
    return next_name


def _lfn_entries(long, short):
    chk = 0
    for b in short:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF
    u = [ord(c) for c in long] + [0]
    u += [0xFFFF] * (-len(u) % 13)
    parts = [u[i:i + 13] for i in range(0, len(u), 13)]
    out = []
    for seq in range(len(parts), 0, -1):
        p = parts[seq - 1]
        e = bytearray(32)
        e[0] = seq | (0x40 if seq == len(parts) else 0)
        for i, at in enumerate((1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)):
            struct.pack_into("<H", e, at, p[i])
        e[11], e[13] = 0x0F, chk
        out.append(bytes(e))
    return out


def make_image(path, files, size_mb=32):
    total = size_mb * 2048 - PART_LBA
    fat_sectors = 64
    root_sectors = ROOT_ENTRIES * 32 // SECTOR
    data_lba = RESERVED + NFATS * fat_sectors + root_sectors
    nclusters = (total - data_lba) // SPC
    assert 4085 <= nclusters < 65525 and nclusters * 2 <= fat_sectors * SECTOR
    csize = SPC * SECTOR
    fat = [0xFFF8, 0xFFFF] + [0] * nclusters
    data = {}                                       # cluster -> bytes
    next_free = [2]
    short = _short_names()

    def alloc(content):
        if not content:
            return 0
        n = (len(content) + csize - 1) // csize
        first = next_free[0]
        for i in range(n):
            c = first + i
            fat[c] = c + 1 if i < n - 1 else 0xFFFF
            data[c] = content[i * csize:(i + 1) * csize].ljust(csize, b"\0")
        next_free[0] += n
        return first

    # Build the tree.
    tree = {}
    for p, content in files.items():
        node = tree
        parts = p.strip("/").split("/")
        for d in parts[:-1]:
            node = node.setdefault(d, {})
        node[parts[-1]] = content

    def entry(name, attr, cluster, size):
        s = short(name) if (len(name) > 12 or name != name.upper() or " " in name) else \
            name.split(".")[0].ljust(8).encode() + (name.split(".")[1] if "." in name else "").ljust(3).encode()
        e = bytearray(32)
        e[0:11] = s
        e[11] = attr
        struct.pack_into("<HI", e, 26, cluster & 0xFFFF, size)
        lfn = _lfn_entries(name, s) if s.decode().strip() != name.replace(".", "").ljust(11).strip() or "~" in s.decode() else []
        return b"".join(lfn) + bytes(e)

    def write_dir(node, parent_cluster):
        # Reserve this directory's cluster(s) first so children follow it.
        entries_size = 64 + sum(32 * (2 + len(k) // 13 + 1) for k in node)
        n = (entries_size + csize - 1) // csize
        first = next_free[0]
        next_free[0] += n
        body = struct.pack("<11sB", b".".ljust(11), 0x10).ljust(26, b"\0") + struct.pack("<HI", first, 0)
        body += struct.pack("<11sB", b"..".ljust(11), 0x10).ljust(26, b"\0") + struct.pack("<HI", parent_cluster, 0)
        for name, v in node.items():
            if isinstance(v, dict):
                body += entry(name, 0x10, write_dir(v, first), 0)
            else:
                body += entry(name, 0x20, alloc(v), len(v))
        for i in range(n):
            fat[first + i] = first + i + 1 if i < n - 1 else 0xFFFF
            data[first + i] = body[i * csize:(i + 1) * csize].ljust(csize, b"\0")
        return first

    root = b""
    for name, v in tree.items():
        root += entry(name, 0x10, write_dir(v, 0), 0) if isinstance(v, dict) else entry(name, 0x20, alloc(v), len(v))
    assert len(root) <= root_sectors * SECTOR

    img = bytearray(size_mb * 1024 * 1024)
    mbr = bytearray(512)
    struct.pack_into("<BBBBBBBBII", mbr, 446, 0, 0, 0, 0, 0x06, 0, 0, 0, PART_LBA, total)
    mbr[510:512] = b"\x55\xaa"
    img[0:512] = mbr
    bs = bytearray(512)
    bs[0:3] = b"\xeb\x3c\x90"
    bs[3:11] = b"FREEDJ  "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, SPC, RESERVED, NFATS, ROOT_ENTRIES, 0, 0xF8, fat_sectors, 63, 255,
                     PART_LBA, total)
    bs[38] = 0x29
    bs[43:54] = b"TESTSTICK  "
    bs[54:62] = b"FAT16   "
    bs[510:512] = b"\x55\xaa"
    base = PART_LBA * SECTOR
    img[base:base + 512] = bs
    fat_bytes = struct.pack("<%dH" % len(fat), *fat)
    for k in range(NFATS):
        o = base + (RESERVED + k * fat_sectors) * SECTOR
        img[o:o + len(fat_bytes)] = fat_bytes
    o = base + (RESERVED + NFATS * fat_sectors) * SECTOR
    img[o:o + len(root)] = root
    for c, b in data.items():
        o = base + (data_lba + (c - 2) * SPC) * SECTOR
        img[o:o + csize] = b
    with open(path, "wb") as f:
        f.write(img)


def anlz(audio_path, bpm, beats=400, first_ms=0):
    """A minimal rekordbox ANLZ0000.DAT: the audio's path (PPTH) and a beat grid (PQTZ)."""
    p = (audio_path + "\0").encode("utf-16-be")
    ppth = b"PPTH" + struct.pack(">III", 16, 16 + len(p), len(p)) + p
    period = 60000.0 / bpm
    entries = b"".join(struct.pack(">HHI", i % 4 + 1, int(round(bpm * 100)), int(first_ms + i * period)) for i in range(beats))
    pqtz = b"PQTZ" + struct.pack(">IIIII", 24, 24 + len(entries), 0, 0x80000, beats) + entries
    body = ppth + pqtz
    return b"PMAI" + struct.pack(">II", 28, 28 + len(body)) + b"\0" * 16 + body
