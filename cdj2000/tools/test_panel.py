#!/usr/bin/env python3
"""Milestone 2, end to end on the emulated CDJ-2000 (cdj-nxs2-qemu):
boot our main board image, then work its front panel through the emulator's
key socket and check (a) the deck events our firmware prints on its console
and (b) the lamps it lights, read back from the emulator's panel-state socket.

  python3 tools/test_panel.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, re, select, socket, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz

from test_link import QEMU, MACHINE, IMAGE


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]; s.close(); return p


def stick():
    """A stick with one short WAV (PLAY needs a track, as on a CDJ)."""
    pcm = b"\0\0\0\0" * 44100 * 4
    fmt = struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16)
    wav = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm
    os.makedirs("build", exist_ok=True)
    make_image("build/test_panel_stick.img", {"/Contents/a.wav": wav, "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT": anlz("/Contents/a.wav", 120)})
    return "build/test_panel_stick.img"


class Rig:
    def __init__(self, image):
        self.keys = free_port()
        self.state = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.state.bind(("127.0.0.1", 0)); self.state.setblocking(False)
        env = dict(os.environ, CDJ_PANEL_KEYSOCK=str(self.keys), CDJ_PANEL_STATESOCK=str(self.state.getsockname()[1]))
        self.p = subprocess.Popen([QEMU, "-M", MACHINE, "-kernel", image, "-display", "none", "-monitor", "none",
                                   "-serial", "stdio", "-serial", "null",
                                   "-drive", f"if=none,id=usbstk,file={stick()},format=raw,snapshot=on",
                                   "-device", "usb-storage,drive=usbstk,port=1"], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, env=env)
        os.set_blocking(self.p.stdout.fileno(), False)
        self.out, self.lamps = "", None

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([self.p.stdout, self.state], [], [], 0.05)
            if self.p.stdout in r:
                self.out += (self.p.stdout.read() or b"").decode(errors="replace")
            if self.state in r:
                try:
                    m = self.state.recv(512).decode()
                    if m.startswith("pnl "): self.lamps = bytes.fromhex(m[4:])
                except BlockingIOError:
                    pass

    def send(self, msg):
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(msg.encode(), ("127.0.0.1", self.keys))

    def expect(self, pattern, secs=3.0):
        end = time.time() + secs
        while time.time() < end:
            if re.search(pattern, self.out): return True
            self.pump(0.05)
        return False

    def lamp(self, off, mask, want, secs=2.0):
        end = time.time() + secs
        while time.time() < end:
            if self.lamps is not None and bool(self.lamps[off] & mask) == want: return True
            self.pump(0.05)
        return False

    def close(self):
        self.p.kill(); self.p.wait()


def main(image):
    rig = Rig(image)
    fails = []
    def check(name, ok):
        print(("ok   " if ok else "FAIL ") + name)
        if not ok: fails.append(name)
    try:
        check("boots and the panel link comes up", rig.expect(r"panel: link up", 15))
        check("a track is loaded", rig.expect(r"deck: loaded a.wav", 5))
        check("idle: CUE lit, PLAY dark", rig.lamp(0x01, 0x10, True) and rig.lamp(0x01, 0x20, False))
        rig.send("0x10:0x01:200")
        check("PLAY press is a play/pause event", rig.expect(r"panel: play/pause down") and rig.expect(r"panel: play/pause up"))
        check("and lights PLAY, CUE goes dark", rig.lamp(0x01, 0x20, True) and rig.lamp(0x01, 0x10, False))
        rig.send("0x10:0x02:200")
        check("CUE press is a cue event and stops it", rig.expect(r"panel: cue down") and rig.lamp(0x01, 0x20, False))
        rig.send("0x13:0x40:200")
        check("MASTER TEMPO toggles its lamp", rig.expect(r"panel: key lock down") and rig.lamp(0x02, 0x20, True))
        rig.send("0x04:0x90:0:lvl")
        check("tempo slider up 1/16 of its travel", rig.expect(r"panel: tempo 562/1000"))
        rig.send("0x0E:0x03:0:lvl")
        check("select knob 3 clicks is browse 3", rig.expect(r"panel: browse 3\b"))
        rig.send("0x09:0x50:0:lvl")
        check("jog counter +80 is jog 80", rig.expect(r"panel: jog 80\b"))
        rig.send("0x14:0x01:200")
        check("a key with no deck event yet (BROWSE) comes back raw", rig.expect(r"panel: key 0x14/0x01 down"))
        if MACHINE == "cdj2000":
            rig.send("0x0F:0x02:0:hold")
            check("DIRECTION lever to REV is reverse", rig.expect(r"panel: reverse down"))
        else:   # the XDJ model holds its lever at FWD (the bit set) in every frame: REV can't be sent
            check("the DIRECTION lever reads FWD: the deck never went into reverse", "panel: reverse" not in rig.out)
        check("every exchange answered", "no answer" not in rig.out)
    finally:
        rig.close()
    print("\n--- console\n" + rig.out.strip())
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else IMAGE))
