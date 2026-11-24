#!/usr/bin/env python3
"""Milestones 5 and 6, end to end on the emulated CDJ-2000: a stick with a WAV
of two known tones; PLAY must start the deck streaming it through our DSP
program to the DAC (McASP1, through the dMAX) in real time and bit for bit,
PLAY again must pause it (silence, the position held), PLAY resume it, the
tempo slider speed it up by its range, and CUE while playing take it back to
the cue point and stop.

The rate comes from the DSP's own count of halves sent and the player's
position, each over 5 s of the deck's time; the content from the DSP's
ping-pong buffer, read through the host-port window with the QEMU monitor:
every frame in it must be the WAV's, in order.

  python3 tools/test_audio.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import math, os, re, socket, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz
from test_link import QEMU

AUDIO = "/Contents/Tones/440 and 660.wav"
SECONDS = 45


def tones():
    frames = []
    for i in range(44100 * SECONDS):
        # Two tones, with the frame's number in their low 11 bits (left: bits
        # 0-10 of it, right: bits 11-21), so every frame in the file is
        # distinct and says where it is.
        l = (int(12000 * math.sin(2 * math.pi * 440 * i / 44100)) & ~2047) | (i & 2047)
        r = (int(12000 * math.sin(2 * math.pi * 660 * i / 44100 + 0.5)) & ~2047) | (i >> 11 & 2047)
        frames.append((l, r))
    pcm = b"".join(struct.pack("<hh", l, r) for l, r in frames)
    fmt = struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16)
    wav = b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(pcm)) + b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    return wav + b"data" + struct.pack("<I", len(pcm)) + pcm, frames


class Monitor:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX); self.s.connect(path); break
            except OSError:
                time.sleep(0.05)
        self.read_prompt()

    def read_prompt(self):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            buf += self.s.recv(65536)
        return buf.decode(errors="replace")

    def words(self, addr, n):
        self.s.send(f"xp /{n}wx 0x{addr:08x}\n".encode())
        out = self.read_prompt()
        return [int(w, 16) for w in re.findall(r"0x([0-9a-f]{8})(?!:)", out.split("\n", 1)[1] if "\n" in out else out)][:n]


def main(image):
    wav, frames = tones()
    os.makedirs("build", exist_ok=True)
    stick = "build/test_audio_stick.img"
    make_image(stick, {AUDIO: wav, "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT": anlz(AUDIO, 120.0)}, size_mb=32)
    idle = open("build/dsp.idle").read().strip()
    keys = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); keys.bind(("127.0.0.1", 0)); kport = keys.getsockname()[1]; keys.close()
    mon = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), f"cdj2000-test-{os.getpid()}.mon")
    env = dict(os.environ, CDJ_DSP_AUDIO="1", CDJ_C6727_IDLE=f"{idle}:0:0xffffffff:4", CDJ_PANEL_KEYSOCK=str(kport))
    p = subprocess.Popen([QEMU, "-M", "cdj2000", "-kernel", image, "-display", "none", "-monitor", f"unix:{mon},server,nowait",
                          "-serial", "stdio", "-serial", "null", "-audio", "none",
                          "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                          "-device", "usb-storage,drive=usbstk,port=1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_audio.qemu.log", "w"), stdin=subprocess.DEVNULL, env=env)
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
            m = re.search(pattern, out[0])
            if m: return m
            pump(0.1)
        return None
    def key(msg):
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(msg.encode(), ("127.0.0.1", kport))
    def reports(after):
        return [(int(h), int(ms)) for h, ms in re.findall(r"dsp: (\d+) halves sent to the DAC, at (\d+) ms", out[0][after:])]

    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)
    try:
        check("the DSP runs our program", wait_for(r"dsp: running", 10) is not None)
        check("the WAV is loaded, cued at its first beat", wait_for(r"deck: loaded 440 and 660.wav \(45\.0 s, 44100 Hz, 400 beats", 15) is not None)
        wait_for(r"panel: link up", 10)
        m = Monitor(mon)
        key("0x10:0x01:200")
        check("PLAY plays", wait_for(r"deck: play at 0 ms", 5) is not None)
        start = len(out[0])
        pump(12)
        r = reports(start)
        if len(r) >= 2:
            (h0, p0), (h1, p1) = r[-2], r[-1]
            rate = (h1 - h0) * 16 / 5.0
            check("the DAC gets 44100 frames a second, in the deck's time", abs(rate - 44100) < 441, f"{rate:.0f}")
            check("and the track moves 5 s in 5 s", abs((p1 - p0) - 5000) < 60, f"{p1 - p0} ms")
        else:
            check("two DSP reports while playing", False, str(r))
        # The ping-pong buffer: 2 halves of 16 frames, words (sample << 16), left then right.
        lookup = {f: i for i, f in enumerate(frames)}
        assert len(lookup) == len(frames), "frames not distinct"
        now_ms = r[-1][1] if r else 0
        ok, where = True, []
        for half in (0, 1):
            w = m.words(0x0C0C6000 + 0x80 * half, 32)
            got = [((w[2 * k] >> 16) - ((w[2 * k] >> 31) << 16), (w[2 * k + 1] >> 16) - ((w[2 * k + 1] >> 31) << 16)) for k in range(16)]
            lo = [v & 0xFFFF for v in w]
            i0 = lookup.get(got[0])
            ok &= i0 is not None and got == frames[i0:i0 + 16] and not any(lo)
            where.append(i0)
        check("the buffer the DAC is fed from holds the WAV's frames, in order, exactly", ok, f"halves from frames {where}")
        at_ms = min(i for i in where if i is not None) * 1000 / 44100 if all(i is not None for i in where) else -1
        check("from where the track is now (within the reports' 5 s)", 0 <= at_ms - now_ms <= 5500, f"buffer at {at_ms:.0f} ms, last report {now_ms} ms")
        key("0x10:0x01:200")
        check("PLAY again pauses", wait_for(r"deck: pause at \d+ ms", 5) is not None)
        start = len(out[0])
        pump(11)
        r = reports(start)
        held = len(r) >= 2 and r[-1][1] == r[-2][1]
        check("paused, the position holds", held, str(r[-2:]))
        silent = all(v == 0 for v in m.words(0x0C0C6000, 64))
        check("and the DAC gets silence (the ring runs dry)", silent)
        check("the DAC is still clocked while paused", len(r) >= 2 and r[-1][0] > r[-2][0])
        key("0x10:0x01:200")
        check("PLAY again resumes where it paused", wait_for(r"deck: play at %d ms" % r[-1][1], 5) is not None if r else False)
        key("0x04:0x90:0:lvl")                       # the slider 1/16 of its travel up: +1.25 % at the 10 % range
        check("the tempo slider reads +1.25 %", wait_for(r"deck: .*\+1\.25 %", 8) is not None)
        start = len(out[0])
        pump(11)
        r = reports(start)
        moved = r[-1][1] - r[-2][1] if len(r) >= 2 else 0
        check("and the track moves 1.25 % faster", abs(moved - 5062.5) < 40, f"{moved} ms in 5 s")
        key("0x10:0x02:200")
        check("CUE while playing goes back to the cue and stops", wait_for(r"deck: pause at 0 ms", 5) is not None)
        start = len(out[0])
        pump(6)
        r = reports(start)
        check("and stays at 0 ms", bool(r) and r[-1][1] == 0, str(r[-1:]))
        log = open("build/test_audio.qemu.log").read()
        check("no fault on either processor", "BL-masked" not in log and "fatal" not in out[0])
    finally:
        p.kill(); p.wait()
        try: os.unlink(mon)
        except OSError: pass
    print("\n--- console\n" + out[0].strip())
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/main.bin"))
