#!/usr/bin/env python3
"""Our image on a real host network: the emulated CDJ-2000 on a tap device
bridged into cdj3k-emu's private bridge djlink0 (off the LAN), with a stick
of a 60 s 120 BPM click track, its grid and an export.pdb (rekordbox ID 41),
so freedj-3000 can FOLLOW it (docs/ethernet.md).  The tap, once, as root:

  ip tuntap add dev cdj2k0 mode tap user $USER && ip link set cdj2k0 master djlink0 && ip link set cdj2k0 up

then:  python3 tools/emu_djlink.py            (console on stdout)
       OPENDECK_LINK_IFACE=djlink0 OPENDECK_SERVE=0 opendeck --follow 2
PLAY: echo -n 0x10:0x01:200 > /dev/udp/127.0.0.1/47123 (CDJ_PANEL_KEYSOCK)."""
import os, struct, subprocess, sys
CDJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(CDJ, "tools"))
from fatimage import make_image, anlz
from pdbimage import export_pdb
from test_link import QEMU

here = os.path.join(CDJ, "build")
sr, secs = 44100, 60
frames = bytearray(sr * secs * 4)
for b in range(secs * 2):                       # a click on every beat (500 ms)
    at = b * sr // 2
    for i in range(400):
        v = int(20000 * (1 - i / 400)) * (1 if i % 20 < 10 else -1)
        struct.pack_into("<hh", frames, (at + i) * 4, v, v)
wav = (b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, sr, sr * 4, 4, 16)
       + b"data" + struct.pack("<I", len(frames)) + bytes(frames))
audio, where = "/Contents/Click 120.wav", "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT"
stick = os.path.join(here, "stick.img")
make_image(stick, {audio: wav, where: anlz(audio, 120.0, beats=120),
                   "/PIONEER/rekordbox/export.pdb": export_pdb([(41, audio, where, "Click 120")])}, size_mb=32)
env = dict(os.environ, CDJ_DSP_AUDIO="1", CDJ_PANEL_KEYSOCK="47123",
           CDJ_C6727_IDLE=open(os.path.join(CDJ, "build/dsp.idle")).read().strip() + ":0:0xffffffff:4")
os.execve(QEMU, [QEMU, "-M", "cdj2000", "-kernel", os.path.join(CDJ, "build/main.bin"), "-display", "none", "-monitor", "none",
                 "-serial", "stdio", "-serial", "null", "-audio", "none",
                 "-netdev", "tap,id=djlink,ifname=cdj2k0,script=no,downscript=no",
                 "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                 "-device", "usb-storage,drive=usbstk,port=1"], env)
