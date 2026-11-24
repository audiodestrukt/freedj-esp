#!/usr/bin/env python3
"""Our image on a real host network: the emulated CDJ-2000 on a tap device
bridged into cdj3k-emu's private bridge djlink0 (off the LAN), with a stick
of a 60 s 120 BPM click track, its grid and an export.pdb (rekordbox ID 41),
so freedj-3000 can FOLLOW it (docs/ethernet.md).  The tap, once, as root:

  ip tuntap add dev cdj2k0 mode tap user $USER && ip link set cdj2k0 master djlink0 && ip link set cdj2k0 up

then:  [BOARD=xdj1000mk2] python3 tools/emu_djlink.py [--app]    (console on stdout; --app: the panel and screen)
       OPENDECK_LINK_IFACE=djlink0 OPENDECK_SERVE=0 opendeck --follow 2
PLAY: echo -n 0x10:0x01:200 > /dev/udp/127.0.0.1/47123 (CDJ_PANEL_KEYSOCK)."""
import os, struct, subprocess, sys, time
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
# --app: cdj-nxs2-qemu's virtual deck app shows the front panel (click keys,
# drag the jog) and our screen over VNC, as tools/demo.py does.
show = "--app" in sys.argv[1:]
EMU = os.path.expanduser(os.environ.get("CDJ_NXS2", "~/sandbox/cdj-nxs2"))
env = dict(os.environ, CDJ_DSP_AUDIO="1", CDJ_PANEL_KEYSOCK="47123",
           CDJ_C6727_IDLE=open(os.path.join(CDJ, "build/dsp.idle")).read().strip() + ":0:0xffffffff:4")
# BOARD=xdj1000mk2: that image on its machine (silent: its DSP is not ported).
BOARD = os.environ.get("BOARD", "cdj2000")
IMAGE = os.path.join(CDJ, "build/main.bin" if BOARD == "cdj2000" else f"build-{BOARD}/main.bin")
cmd = [QEMU, "-M", BOARD, "-kernel", IMAGE, "-monitor", "none",
       "-serial", "stdio", "-serial", "null", "-audio", os.environ.get("AUDIODEV", "pa" if show else "none"),
       "-netdev", "tap,id=djlink,ifname=cdj2k0,script=no,downscript=no",
       "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
       "-device", "usb-storage,drive=usbstk,port=1"]
if not show:
    os.execve(QEMU, cmd + ["-display", "none"], env)
vnc = 5960
env.update(CDJ_PANEL_STATESOCK="47124", CDJ_BF531_FLASH=os.path.join(CDJ, "build/gui.flash"))
os.makedirs(os.path.join(here, "qemu-data/keymaps"), exist_ok=True)
open(os.path.join(here, "qemu-data/keymaps/en-us"), "w").write("# empty: the panel app's keys go over its own socket\n")
emu = subprocess.Popen(cmd + ["-L", os.path.join(here, "qemu-data"), "-display", "none", "-vnc", f"127.0.0.1:{vnc - 5900}"], env=env)
app = subprocess.Popen([os.path.join(EMU, ".venv/bin/python"), "-m", "app.virtual_deck", "--model", BOARD,
                        "--panel-port", "47123", "--state-port", "47124", "--screen", "face", "--vnc-base", str(vnc - 1)],
                       cwd=EMU, stdout=open(os.path.join(here, "emu_djlink.app.log"), "w"), stderr=subprocess.STDOUT)
try:
    while emu.poll() is None and app.poll() is None: time.sleep(0.2)
finally:
    for q in (app, emu):
        if q.poll() is None: q.terminate()
