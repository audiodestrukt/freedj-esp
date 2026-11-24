#!/usr/bin/env python3
"""Milestone 6, the deck's screen, end to end on the emulated CDJ-2000: our
display program drawing what our main board sends it.  A stick with a 120 BPM
click track; screendumps of the panel (QEMU's monitor) at load, while
playing and while paused, checked for what a deck's screen must do:

  - the splash goes once a track is loaded, and the deck's screen is drawn:
    the title, the red playhead, the cue marker, the waveform
  - while playing the waveform scrolls, the overview's marker moves to where
    the track is, the time left counts down, one beat-in-bar box is lit
  - paused, the waveform, the playhead and the readouts stand still

  python3 tools/test_screen.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import math, os, re, socket, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz
from test_link import QEMU

W, H = 480, 255
HEAD_X, WAVE_Y0, WAVE_Y1, OV_Y0, OV_Y1, INFO_Y = 200, 26, 126, 132, 156, 164
SECONDS = 60


def click_track():
    """A kick on every beat at 120 BPM: a decaying 60 Hz thump, so the waveform has shape."""
    pcm = bytearray()
    for i in range(44100 * SECONDS):
        t = (i % 22050) / 44100
        v = int(20000 * math.exp(-t * 18) * math.sin(2 * math.pi * 60 * t)) if t < 0.25 else 0
        pcm += struct.pack("<hh", v, v)
    fmt = struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16)
    return b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(pcm)) + bytes(pcm)


def ppm(path):
    d = open(path, "rb").read()
    head, rest = d.split(b"\n", 1)
    size, rest = rest.split(b"\n", 1)
    _, px = rest.split(b"\n", 1)
    assert head == b"P6" and size == b"480 255"
    return px


def at(px, x, y):
    i = (y * W + x) * 3
    return px[i], px[i + 1], px[i + 2]


def band(px, y0, y1):
    return px[y0 * W * 3:y1 * W * 3]


def main(image):
    os.makedirs("build", exist_ok=True)
    stick = "build/test_screen_stick.img"
    make_image(stick, {"/Contents/Kick Test.wav": click_track(),
                       "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT": anlz("/Contents/Kick Test.wav", 120.0, beats=120)})
    keys = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); keys.bind(("127.0.0.1", 0)); kport = keys.getsockname()[1]; keys.close()
    mon = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"cdj2000-screen-{os.getpid()}.mon")
    env = dict(os.environ, CDJ_BF531_FLASH="build/gui.flash", CDJ_DSP_AUDIO="1", CDJ_PANEL_KEYSOCK=str(kport),
               CDJ_C6727_IDLE=open("build/dsp.idle").read().strip() + ":0:0xffffffff:4")
    p = subprocess.Popen([QEMU, "-M", "cdj2000", "-kernel", image, "-display", "none", "-monitor", f"unix:{mon},server,nowait",
                          "-serial", "stdio", "-serial", "null", "-audio", "none",
                          "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                          "-device", "usb-storage,drive=usbstk,port=1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_screen.qemu.log", "w"), stdin=subprocess.DEVNULL, env=env)
    os.set_blocking(p.stdout.fileno(), False)
    out = [""]
    def pump(secs):
        end = time.time() + secs
        while time.time() < end:
            out[0] += (p.stdout.read() or b"").decode(errors="replace")
            time.sleep(0.05)
    def wait_for(pattern, secs):
        end = time.time() + secs
        while time.time() < end:
            if re.search(pattern, out[0]): return True
            pump(0.1)
        return False
    def key(m):
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(m.encode(), ("127.0.0.1", kport))
    mons = socket.socket(socket.AF_UNIX)
    def dump(name):
        path = os.path.abspath(f"build/test_screen_{name}.ppm")
        if os.path.exists(path): os.unlink(path)
        mons.send(f"screendump {path}\n".encode())
        for _ in range(60):
            if os.path.exists(path) and os.path.getsize(path) >= W * H * 3: break
            time.sleep(0.05)
        time.sleep(0.2)
        try: mons.recv(65536)
        except BlockingIOError: pass
        return ppm(path)

    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    def marker_x(px):           # the overview's position marker: the white column standing out above it
        y = OV_Y0 - 1
        xs = [x for x in range(W) if min(at(px, x, y)) > 200]
        return sum(xs) / len(xs) if xs else -1
    try:
        for _ in range(50):
            try: mons.connect(mon); break
            except OSError: time.sleep(0.1)
        mons.setblocking(False)
        check("the track loads", wait_for(r"deck: loaded Kick Test\.wav", 25))
        pump(1.5)
        a = dump("loaded")
        red = at(a, HEAD_X, (WAVE_Y0 + WAVE_Y1) // 2)
        check("the deck's screen replaces the splash: the red playhead", red[0] > 200 and red[1] < 90 and red[2] < 90, str(red))
        cue = at(a, HEAD_X + 3, WAVE_Y0 + 1)        # beside the playhead, which is drawn over its middle
        check("the cue marker over it (the first beat)", cue[0] > 200 and 100 < cue[1] < 190 and cue[2] < 60, str(cue))
        title = sum(1 for x in range(8, 150) for y in range(4, 18) if min(at(a, x, y)) > 180)
        check("the title is written", title > 40, f"{title} bright pixels")
        blue = sum(1 for x in range(HEAD_X + 2, W) for y in range(WAVE_Y0, WAVE_Y1) if at(a, x, y)[2] > 200 and at(a, x, y)[0] < 120)
        check("the waveform ahead of the playhead is drawn", blue > 500, f"{blue} blue pixels")
        key("0x10:0x01:200")
        check("PLAY plays", wait_for(r"deck: play at", 5))
        pump(3)
        b = dump("playing1")
        pump(1.5)
        c = dump("playing2")
        check("playing, the waveform scrolls", band(b, WAVE_Y0, WAVE_Y1) != band(c, WAVE_Y0, WAVE_Y1))
        mb, mc = marker_x(b), marker_x(c)
        check("the overview's marker moves on", 0 <= mb < mc, f"x {mb:.1f} then {mc:.1f}")
        check("the time left counts down", band(b, INFO_Y + 14, INFO_Y + 50) != band(c, INFO_Y + 14, INFO_Y + 50))
        lit = [i for i in range(4) if max(at(c, 184 + i * 18 + 7, INFO_Y + 31)) > 200]
        check("one beat-in-bar box is lit", len(lit) == 1, str(lit))
        key("0x10:0x01:200")
        check("PLAY again pauses", wait_for(r"deck: pause at", 5))
        pump(1)
        d = dump("paused1")
        m = re.search(r"deck: pause at (\d+) ms", out[0])
        expect = W * int(m.group(1)) / 1000 / SECONDS if m else -1
        check("paused, the overview's marker is where the track stopped", m and abs(marker_x(d) - expect) <= 1.5,
              f"marker {marker_x(d):.1f}, {m.group(1) if m else '-'} ms -> {expect:.1f}")
        pump(1.5)
        e = dump("paused2")
        # (The overview may still fill in: the deck decodes ahead while paused.)
        check("paused, the waveform, playhead and readouts stand still",
              band(d, 0, OV_Y0) == band(e, 0, OV_Y0) and band(d, INFO_Y, H) == band(e, INFO_Y, H))
        check("and the overview's marker too", abs(marker_x(d) - marker_x(e)) < 0.01)
        check("no fault on any processor", "BL-masked" not in open("build/test_screen.qemu.log").read()
              and "SPORT1 RX queue full" not in open("build/test_screen.qemu.log").read())
    finally:
        p.kill(); p.wait()
        try: os.unlink(mon)
        except OSError: pass
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/main.bin"))
