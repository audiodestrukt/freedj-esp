#!/usr/bin/env python3
"""A minimal rekordbox export.pdb for the tests, written from scratch: the
header page, and a track table of one empty page and one page of rows.
Only what a reader needs to tie a track's rekordbox ID to its paths (the ID
at 0x48, the 21 string offsets at 0x5e: 14 the analysis path, 17 the title,
19 the file name, 20 the file path).  Layout as Deep Symmetry documents it.

  export_pdb([(id, "/Contents/a.wav", "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT", "Title"), ...])
"""
import struct

PAGE = 4096


def _string(s):
    b = s.encode("ascii")
    if len(b) < 126:
        return bytes([(len(b) + 1) << 1 | 1]) + b          # short ASCII: length with the header byte, odd
    return struct.pack("<BHB", 0x40, len(b) + 4, 0) + b      # long ASCII


def _track_row(tid, path, anlz, title):
    strings = [""] * 21
    strings[14], strings[17], strings[19], strings[20] = anlz, title, path.rsplit("/", 1)[-1], path
    head = bytearray(0x5e + 2 * 21)
    struct.pack_into("<HH", head, 0, 0x24, 0)
    struct.pack_into("<I", head, 0x08, 44100)               # sample rate
    struct.pack_into("<I", head, 0x38, 12000)               # tempo x 100
    struct.pack_into("<I", head, 0x48, tid)
    body = bytearray()
    for i, s in enumerate(strings):
        struct.pack_into("<H", head, 0x5e + 2 * i, len(head) + len(body))
        body += _string(s)
    row = bytes(head + body)
    return row + b"\0" * (-len(row) % 4)


def _page(index, kind, next_page, rows, flags):
    p = bytearray(PAGE)
    heap, offsets = bytearray(), []
    for r in rows:
        offsets.append(len(heap))
        heap += r
    assert 0x28 + len(heap) + 0x24 * ((len(rows) + 15) // 16) <= PAGE, "rows overflow the page"
    struct.pack_into("<IIII", p, 0, 0, index, kind, next_page)
    struct.pack_into("<IIBBBB", p, 0x10, 1, 0, len(rows) & 0xff, 0, 0, flags)
    struct.pack_into("<HHHHHH", p, 0x1c, PAGE - 0x28 - len(heap), len(heap), 0, len(rows), 0, 0)
    p[0x28:0x28 + len(heap)] = heap
    for g in range((len(rows) + 15) // 16):                  # row groups from the page's end
        end = PAGE - g * 0x24
        group = offsets[g * 16:g * 16 + 16]
        for i, off in enumerate(group):
            struct.pack_into("<H", p, end - 4 - 2 * (i + 1), off)
        struct.pack_into("<HH", p, end - 4, (1 << len(group)) - 1, 0)
    return bytes(p)


def export_pdb(tracks):
    head = bytearray(PAGE)
    # unknown, page size, tables, next unused page, unknown, sequence, gap; then each table:
    # type, empty candidate, first page, last page.
    struct.pack_into("<IIIIIII", head, 0, 0, PAGE, 1, 3, 5, 1, 0)
    struct.pack_into("<IIII", head, 0x1c, 0, 0, 1, 2)
    rows = [_track_row(*t) for t in tracks]
    return bytes(head) + _page(1, 0, 2, [], 0x64) + _page(2, 0, 3, rows, 0x34)
