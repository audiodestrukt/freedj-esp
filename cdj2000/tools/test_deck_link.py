#!/usr/bin/env python3
"""The deck engine on Pro DJ Link, end to end on the emulated CDJ-2000: a
stick with a 120 BPM track (an MP3, if ffmpeg is here to make one, else a
WAV), PLAY, and the deck's beat packets caught on its private segment: one
per beat, 500 ms apart, 120.00 BPM, the beat's place in the bar counting
1-4, and the status saying it plays.  Then the tempo slider +1.25 %: the
beats close up to 493.8 ms and the pitch field says +1.25 %.

  python3 tools/test_deck_link.py build/main.bin      (CDJ_QEMU= another qemu)
"""
import os, re, select, shutil, socket, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimage import make_image, anlz
from test_link import QEMU, Segment, OUR_MAC, MAGIC


def track():
    if shutil.which("ffmpeg"):
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=60:sample_rate=44100",
                        "-ac", "2", "-b:a", "192k", "build/deck_link.mp3"], check=True)
        return "/Contents/beat.mp3", open("build/deck_link.mp3", "rb").read()
    pcm = b"\0\0\0\0" * 44100 * 60
    fmt = struct.pack("<HHIIHH", 1, 2, 44100, 44100 * 4, 4, 16)
    return "/Contents/beat.wav", (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<I", 16) + fmt
                                  + b"data" + struct.pack("<I", len(pcm)) + pcm)


def main(image):
    os.makedirs("build", exist_ok=True)
    path, data = track()
    stick = "build/test_deck_link_stick.img"
    make_image(stick, {path: data, "/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT": anlz(path, 120.0, beats=200)})
    seg = Segment()
    keys = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); keys.bind(("127.0.0.1", 0)); kport = keys.getsockname()[1]; keys.close()
    env = dict(os.environ, CDJ_DSP_AUDIO="1", CDJ_PANEL_KEYSOCK=str(kport),
               CDJ_C6727_IDLE=open("build/dsp.idle").read().strip() + ":0:0xffffffff:4")
    p = subprocess.Popen([QEMU, "-M", "cdj2000", "-kernel", image, "-display", "none", "-monitor", "none",
                          "-serial", "stdio", "-serial", "null", "-audio", "none",
                          "-netdev", f"socket,id=djlink,mcast={seg.group}:{seg.port},localaddr=127.0.0.1",
                          "-drive", f"if=none,id=usbstk,file={stick},format=raw,snapshot=on",
                          "-device", "usb-storage,drive=usbstk,port=1"],
                         stdout=subprocess.PIPE, stderr=open("build/test_deck_link.qemu.log", "w"), stdin=subprocess.DEVNULL, env=env)
    os.set_blocking(p.stdout.fileno(), False)
    out, beats = "", []          # (time, bpm, pitch, beat in bar)

    def pump(secs):
        nonlocal out
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([p.stdout, seg.rx], [], [], 0.01)
            if p.stdout in r:
                out += (p.stdout.read() or b"").decode(errors="replace")
            while True:
                try:
                    f = seg.rx.recv(2048)
                except BlockingIOError:
                    break
                if f[6:12] != OUR_MAC or f[12:14] != b"\x08\x00" or f[23] != 17:
                    continue
                u = f[14 + (f[14] & 15) * 4:]
                d = u[8:struct.unpack("!H", u[4:6])[0]]
                if struct.unpack("!H", u[2:4])[0] == 50001 and d[:10] == MAGIC and d[0x0a] == 0x28 and len(d) >= 0x60:
                    beats.append((time.time(), struct.unpack("!H", d[0x5a:0x5c])[0] / 100,
                                  struct.unpack("!I", d[0x54:0x58])[0] / 0x100000, d[0x5c]))

    def wait_for(pattern, secs):
        end = time.time() + secs
        while time.time() < end:
            if re.search(pattern, out): return True
            pump(0.1)
        return False

    def key(msg):
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(msg.encode(), ("127.0.0.1", kport))

    fails = []
    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))
        if not ok: fails.append(name)

    def grade(label, bpm, pitch, period_ms):
        b = beats[2:]                                   # past the start
        gaps = [1000 * (y[0] - x[0]) for x, y in zip(b, b[1:])]
        mean = sum(gaps) / len(gaps) if gaps else 0
        check(f"{label}: a beat packet every {period_ms:.1f} ms", len(gaps) >= 8 and abs(mean - period_ms) < period_ms * 0.01,
              f"{len(b)} beats, mean {mean:.1f} ms, {min(gaps or [0]):.0f}..{max(gaps or [0]):.0f}")
        check(f"{label}: {bpm:.2f} BPM, pitch {pitch:+.2%}", all(x[1] == bpm and abs(x[2] - 1 - pitch) < 0.0002 for x in b),
              f"{sorted(set((x[1], round(x[2] - 1, 4)) for x in b))}")
        seq = [x[3] for x in b]
        check(f"{label}: the bar position counts 1 2 3 4", len(seq) > 4 and all(y == x % 4 + 1 for x, y in zip(seq, seq[1:])), str(seq[:12]))

    try:
        check("the track loads", wait_for(r"deck: loaded beat\.(mp3|wav)", 25), path)
        wait_for(r"I \(link\) on the network", 10)
        key("0x10:0x01:200")
        check("PLAY plays", wait_for(r"deck: play at 0 ms", 5))
        beats.clear()
        pump(8)
        grade("at +0 %", 120.0, 0.0, 500.0)
        key("0x04:0x90:0:lvl")
        wait_for(r"panel: tempo", 3)
        pump(1)
        beats.clear()
        pump(8)
        grade("at +1.25 %", 120.0, 0.0125, 500.0 / 1.0125)
        check("no fault", "fatal" not in out and "BL-masked" not in open("build/test_deck_link.qemu.log").read())
    finally:
        p.kill(); p.wait()
    print("\n--- console\n" + "\n".join(l for l in out.splitlines() if not l.startswith("panel:")))
    print(f"\n{'all passed' if not fails else str(len(fails)) + ' failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/main.bin"))
