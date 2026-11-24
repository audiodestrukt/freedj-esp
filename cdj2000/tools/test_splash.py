#!/usr/bin/env python3
"""Milestone 6, step 1, on the emulated CDJ-2000: the display board boots our
program from its flash (build/gui.flash) and the panel shows the boot splash,
every pixel as tools/splash.py drew it (RGB555, as the panel takes it).

  python3 tools/test_splash.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, socket, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_link import QEMU


def main(image):
    mon = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"cdj2000-splash-{os.getpid()}.mon")
    shot = os.path.abspath("build/test_splash.ppm")
    if os.path.exists(shot):
        os.unlink(shot)
    env = dict(os.environ, CDJ_BF531_FLASH="build/gui.flash")
    p = subprocess.Popen([QEMU, "-M", "cdj2000", "-kernel", image, "-display", "none", "-monitor", f"unix:{mon},server,nowait",
                          "-serial", "null", "-serial", "null"],
                         stdout=subprocess.DEVNULL, stderr=open("build/test_splash.qemu.log", "w"), env=env)
    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    try:
        time.sleep(3)
        s = socket.socket(socket.AF_UNIX)
        for _ in range(50):
            try: s.connect(mon); break
            except OSError: time.sleep(0.1)
        s.recv(4096)
        s.send(f"screendump {shot}\n".encode())
        for _ in range(50):
            if os.path.exists(shot) and os.path.getsize(shot) > 0: break
            time.sleep(0.1)
        time.sleep(0.3)
    finally:
        p.kill(); p.wait()
        try: os.unlink(mon)
        except OSError: pass
    log = open("build/test_splash.qemu.log").read()
    check("the display board boots our LDR from its flash", "booting the flash's LDR at 0x10000" in log)
    data = open(shot, "rb").read() if os.path.exists(shot) else b""
    parts = data.split(b"\n", 3)
    ok = len(parts) == 4 and parts[0] == b"P6" and parts[1] == b"480 255"
    check("the panel shows a 480 x 255 frame", ok, parts[1].decode(errors="replace") if len(parts) > 1 else "no screendump")
    if ok:
        got = parts[3]
        raw = open("build/splash.rgb555", "rb").read()
        want = bytearray()
        for i in range(0, len(raw), 2):
            v = raw[i] | raw[i + 1] << 8
            want += bytes(((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31))
        diff = sum(1 for i in range(0, len(want), 3) if got[i:i + 3] != want[i:i + 3])
        check("every pixel is the splash's", diff == 0, f"{diff} pixels differ")
        lime = sum(1 for i in range(0, len(got), 3) if got[i] > 180 and got[i + 1] > 230 and got[i + 2] < 40)
        check("with the audiodestrukt mark in lime", lime > 300, f"{lime} lime pixels")
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/main.bin"))
