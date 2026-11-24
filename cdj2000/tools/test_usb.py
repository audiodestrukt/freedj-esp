#!/usr/bin/env python3
"""Milestone 4, end to end on the emulated CDJ-2000: a stick image made here
(tools/fatimage.py: FAT16 in a partition, long names, multi-cluster files),
attached as a USB mass-storage device; our firmware must enumerate it, mount
it, and list its tracks through freedj-esp's rekordbox scan.

  python3 tools/test_usb.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz
from test_link import QEMU, MACHINE, IMAGE

TRACKS = [
    ("/Contents/Test Artist/A long track name, with spaces.wav", 120.0, "P001/00000001"),
    ("/Contents/Other Artist/Second Album/second.wav", 128.0, "P002/00000002"),
]


def main(image):
    files = {}
    for audio, bpm, where in TRACKS:
        files[audio] = b"RIFF" + bytes(range(256)) * 40          # 10 KB: five clusters
        files[f"/PIONEER/USBANLZ/{where}/ANLZ0000.DAT"] = anlz(audio, bpm)   # 3.3 KB: two clusters
    stick = "build/test_stick.img"
    os.makedirs("build", exist_ok=True)
    make_image(stick, files)
    p = subprocess.run(["timeout", "20", QEMU, "-M", MACHINE, "-kernel", image, "-display", "none", "-monitor", "none",
                        "-serial", "stdio", "-serial", "null",
                        "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                        "-device", "usb-storage,drive=usbstk,port=1"],
                       capture_output=True, text=True)
    out = p.stdout
    fails = []
    def check(name, ok):
        print(("ok   " if ok else "FAIL ") + name)
        if not ok: fails.append(name)
    check("enumerates the stick as USB mass storage", re.search(r"usb: device 46f4:0001", out) is not None)
    check("reads its capacity (32 MB)", "usb: stick ready, 65536 blocks of 512 bytes (32 MB)" in out)
    for audio, bpm, where in TRACKS:
        check(f"lists {audio} at {bpm:.2f} BPM (long names, nested folders, a 2-cluster ANLZ)",
              f"/usb{audio}  ({bpm:.2f} BPM; analysis /usb/PIONEER/USBANLZ/{where}/ANLZ0000.DAT; id 0)" in out)   # no export.pdb: no ID
    check("two tracks in all", "usb: 2 analysed tracks on the stick" in out)
    check("no fault", "fatal" not in out and "BL-masked" not in p.stderr)
    print("\n--- console\n" + out.strip())
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else IMAGE))
